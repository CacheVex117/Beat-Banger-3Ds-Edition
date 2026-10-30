/**
 * @file main.c
 * @brief Beat Banger native 3DS / New 2DS XL frontend: menus, dual-screen gameplay, settings and saves.
 *
 * libctru + Citro2D + NDSP + RomFS drive the portable rhythm core (native3ds/core/).
 * One timeline: chart_time = audio_position + calibration - song_offset (see rhythm_core.h); notes, character
 * animation, camera, shutters, videos, SFX one-shots and judgments all read the core's chart time.
 *
 * Top screen    : menus, then the unobstructed character scene (art, camera, shutters, climax videos).
 * Bottom screen : four-lane rhythm highway, judgments and rhythm HUD during play (plain black in menus).
 *                 A hidden text overlay (hold L+R, press Y) shows diagnostics. No other bottom UI.
 *
 * Stages live in romfs:/stages/<id>/ (index in romfs:/stages/index.txt, built by tools/build_stage.py).
 * Controls (settings): face buttons B/A/Y/X, D-pad Down/Right/Left/Up, or both; START pauses.
 */
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include <citro2d.h>

#include "rhythm_core.h"
#include "chart_loader.h"
#include "input_map.h"
#include "save_data.h"
#include "audio_ndsp.h"
#include "anim.h"
#include "sfx.h"
#include "video.h"
#include "game_view.h"
#include "save_io.h"
#include "diag.h"
#include "progress.h"
#include "extras.h"
#include "screens.h"
#include "collection_ui.h"
#include "gallery_ui.h"
#include "music_ui.h"
#include "sceneview_ui.h"
#include "ui_art.h"
#include "menu_ui.h"
#include "cutscene.h"
#include "font.h"
#include "ui_motion.h"
#include "ui_sfx.h"
#include "flow_ui.h"
#include "companion.h"
#include "mods_ui.h"
#include "boot_ui.h"
#include "theme.h"
#include "hw_profile.h"

typedef enum {
    APP_MENU_MAIN = 0,
    APP_MENU_LEVELS,
    APP_MENU_DIFF,
    APP_MENU_SETTINGS,
    APP_MENU_CONFIRM_RESET,
    APP_MENU_MODS,   /* difficulty > modifiers (note speed, no HUD, no beat bar) */
    APP_SCREEN,      /* shop / extras / achievements / power-up selection (screens.c) */
    APP_CUTSCENE,    /* pre-level (before loading) and post-level (after results) cutscenes */
    APP_PLAYING,
    APP_PAUSED,
    APP_FAIL_TAPE,   /* music tape-stop after failing */
    APP_GAMEOVER,
    APP_OUTRO,       /* all notes done, waiting for the song and videos to finish */
    APP_RESULTS,
    APP_TITLE,       /* boot sequence: 18+ warning, fan-port disclaimer, Bunfan intro (boot_ui.c), then the main menu */
    APP_CALIB,       /* Settings > Gameplay > Calibration Menu */
    APP_COUNTIN,     /* RESUME from pause: the original's 3 s countdown, then play continues */
    APP_LOADING,     /* stage loading screen (animates briefly, then the blocking load runs under its last frame) */
    APP_MODMGR       /* Main menu > Mods: hub (Library | Modifiers), library, a mod's screen (mods_ui.c) */
} app_state_t;

static const char* app_state_name(app_state_t s) {
    switch (s) {
        case APP_MENU_MAIN:          return "MENU";
        case APP_MENU_LEVELS:        return "LEVELS";
        case APP_MENU_DIFF:          return "DIFFICULTY";
        case APP_MENU_SETTINGS:      return "SETTINGS";
        case APP_MENU_CONFIRM_RESET: return "CONFIRM";
        case APP_MENU_MODS:          return "MODIFIERS";
        case APP_SCREEN:             return "SCREEN";
        case APP_CUTSCENE:           return "CUTSCENE";
        case APP_PLAYING:            return "PLAYING";
        case APP_PAUSED:            return "PAUSED";
        case APP_FAIL_TAPE:          return "FAIL-TAPE";
        case APP_GAMEOVER:           return "GAME OVER";
        case APP_OUTRO:              return "OUTRO";
        case APP_RESULTS:            return "RESULTS";
        case APP_TITLE:              return "TITLE";
        case APP_CALIB:              return "CALIBRATION";
        case APP_COUNTIN:            return "COUNT-IN";
        case APP_LOADING:            return "LOADING";
        case APP_MODMGR:             return "MODS";
        default:                     return "?";
    }
}

#define MAX_STAGES 64   /* built-in stages + SD-card mods (MOD2DS) */
#define MOD_ROOT "sdmc:/3ds/bb_mods/stages"   /* MOD2DS package (../mod2ds/SPEC.md): same layout as romfs:/stages */
typedef struct {
    char id[32], title[48], character[24], act[24], act_name[32];
    int  act_index, level_index, level_enum;
    int  card_img;   /* pre-level cutscene image shown framed on the level-select top screen */
    int  prereq;     /* meta.cfg level_prereq: the item that opens the level (0 = open from the start) */
    bool has_prereq; /* index field 9 present (built-in stages); SD mods keep the act order instead */
    char root[28];   /* "romfs:/stages" or MOD_ROOT */
    bool mod;        /* from the SD card: listed in Mods > Library, not in the volumes */
    int8_t legacy;   /* converted pre-release (Legacy) mod (MOD2DS writes legacy.txt in its folder): -1 not checked yet */
    float  art_u0, art_u1;   /* legacy.txt "art u0 u1": the art's span of a frame cell (book layout crop) */
} stage_info_t;
static stage_info_t g_stages[MAX_STAGES];
static int g_stage_count = 0;

#define MAX_ACTS 8
static char g_act_ids[MAX_ACTS][24];
static char g_act_names[MAX_ACTS][32];
static int  g_act_count = 0;

static const char* const k_diff_name[3]  = { "virgin", "normal", "expert" };
static const char* const k_ctrl_label[BB_CTRL_COUNT] = { "Buttons (B A Y X)", "D-Pad", "Hybrid (both)", "Shoulders (L ZL ZR R)" };

/* Heavy structs live in static storage */
static bb_rhythm_core_t  g_core;
static bb_chart_t        g_chart;
static bb_audio_stream_t g_audio;
static bb_anim_t         g_anim;
static bb_sfx_t          g_sfx;
static bb_sfx_t          g_sfx_common;     /* romfs:/audio/common.bbsfx: miss / damage / glass for banks without them (SD-card mods) */
static bool              g_sfx_common_tried = false;

/* A shared clip from the level's bank, else from the common bank. */
static void play_shared_sfx(const char* name) {
    if (!bb_sfx_play_named(&g_sfx, name)) bb_sfx_play_named(&g_sfx_common, name);
}
static bb_video_t        g_video[BB_VIDEO_MAX];
static int               g_video_count = 0;
static bb_view_t         g_view;
static bb_save_t         g_save;
static bb_extras_t       g_extras;
static bb_screens_t      g_scr;
static char                g_track_buf[40];
static uint64_t            g_track_t0;            /* when the current scene track / level preview started */
static bool                g_track_preview;       /* level-select preview: 1 s fade-in from -40 dB (SongPreview.play_preview_music) */
static const char*       g_menu_track = NULL;   /* scene music currently streaming ("main"/"shop"/"extras") */
static int               g_powerup = 0;
static bb_video_t        g_intro;          /* boot: the Bunfan Studios intro (romfs:/boot/intro.bbvid) */
static bb_video_t        g_egg;            /* warning screen, B: a chroma-keyed clip over the warning, then the game exits (romfs:/boot/egg.bbvid) */
static bool              g_egg_on = false;
static uint64_t          g_egg_t0 = 0;
static bool              g_intro_ok = false;
#define BOOT_AUTO_MS 300000u               /* boot screens 2 and 3 move on by themselves after 5 minutes (the intro: when it ends) */   /* power-up item active for the next/current run (0 none) */

static int  g_loaded_stage = -1;
static bool g_chart_ok = false;
static bb_load_status_t g_load_status = BB_LOAD_OK;

/* Can the player open this level? The one condition the level list, its lock art and every launch path share.
 * Built-in levels (BaseLevelNode.gd): open when their level_prereq item is owned (501 -> 502 -> ... -> 507; After Class needs the
 * Dorm Key read, egg 901); SD mods: the act order (the first level of a pack is open). The dev-build unlock-all opens everything. */
static bool stage_open(int i) {
    const stage_info_t* st = &g_stages[i];
    return bb_level_open(&g_save, st->has_prereq, st->prereq, st->act, st->level_index);
}

static void stage_path(char* out, size_t n, int stage, const char* rel) {
    snprintf(out, n, "%.27s/%.31s/%s", g_stages[stage].root, g_stages[stage].id, rel);
}

/* A Legacy mod plays sideways (Scroll Mode Side, notes from the right) unless the LEGACY LAYOUT modifier is switched off. */
static bool stage_is_legacy(int stage) {
    if (stage < 0 || stage >= g_stage_count || !g_stages[stage].mod) return false;
    if (g_stages[stage].legacy < 0) {
        char path[96];
        stage_path(path, sizeof(path), stage, "legacy.txt");
        FILE* f = fopen(path, "r");
        g_stages[stage].legacy = f != NULL;
        g_stages[stage].art_u0 = 0.2656f;   /* 600 of 1280: the Legacy template frame */
        g_stages[stage].art_u1 = 0.7344f;
        if (f) {
            char ln[96];
            while (fgets(ln, sizeof(ln), f)) {
                float a, b;
                if (sscanf(ln, "art %f %f", &a, &b) == 2 && a >= 0.0f && b <= 1.0f && b - a > 0.05f) {
                    g_stages[stage].art_u0 = a;
                    g_stages[stage].art_u1 = b;
                }
            }
            fclose(f);
        }
    }
    return g_stages[stage].legacy > 0;
}

/* Mods > Library: the SD stages in index order (MOD2DS sorts them by pack, then level index) */
static int g_mod_rows[MAX_STAGES];
static int g_mod_count = 0;

/* Mod levels on the card that did not fit the list (MAX_STAGES is shared with the built-in stages): the Mods hub gauge counts
 * them, so a card over the limit reads e.g. 60/56 Full. Ids are kept so a level in index.txt and in a folder counts once. */
#define MAX_MOD_OVERFLOW 128
static char g_overflow_ids[MAX_MOD_OVERFLOW][32];
static int  g_mod_overflow = 0;

static bool mod_known(const char* id) {
    for (int i = 0; i < g_stage_count; i++) if (strcmp(g_stages[i].id, id) == 0) return true;
    for (int i = 0; i < g_mod_overflow && i < MAX_MOD_OVERFLOW; i++) if (strcmp(g_overflow_ids[i], id) == 0) return true;
    return false;
}

static void note_mod_overflow(const char* id) {
    if (mod_known(id)) return;
    if (g_mod_overflow < MAX_MOD_OVERFLOW) snprintf(g_overflow_ids[g_mod_overflow], sizeof(g_overflow_ids[0]), "%.31s", id);
    g_mod_overflow++;
}

/* One stage index row (id|title|character|act|act_name|act_index|level_index|level_enum[|prereq]), trailing newline removed.
 * SD rows are mods: they keep their pack as the act id (progress is per pack) but do not become volumes (22 installed packs would
 * not fit MAX_ACTS). Duplicate ids are ignored: a mod can never shadow a built-in stage (or another mod). */
static void add_stage_row(char* line, const char* stage_root, bool mod) {
    size_t ll = strlen(line);
    while (ll > 0 && (line[ll - 1] == '\n' || line[ll - 1] == '\r')) line[--ll] = '\0';
    char* fld[BB_INDEX_FIELDS] = { 0 };
    const int nf = bb_index_split(line, fld, BB_INDEX_FIELDS);   /* 9 fields: the level_prereq is the ninth (it used to stop at 8) */
    if (nf < 3 || !fld[0][0]) {
        return;
    }
    for (int i = 0; i < g_stage_count; i++) {
        if (strcmp(g_stages[i].id, fld[0]) == 0) return;
    }
    if (g_stage_count >= MAX_STAGES) {
        if (mod) note_mod_overflow(fld[0]);
        return;
    }
    stage_info_t* s = &g_stages[g_stage_count++];
    snprintf(s->root, sizeof(s->root), "%s", stage_root);
    s->mod = mod;
    s->legacy = -1;
    if (mod) g_mod_rows[g_mod_count++] = g_stage_count - 1;
    snprintf(s->id, sizeof(s->id), "%.31s", fld[0]);
    snprintf(s->title, sizeof(s->title), "%.47s", fld[1]);
    snprintf(s->character, sizeof(s->character), "%.23s", fld[2]);
    snprintf(s->act, sizeof(s->act), "%.23s", nf > 3 ? fld[3] : "bb_vol_1");
    snprintf(s->act_name, sizeof(s->act_name), "%.31s", nf > 4 ? fld[4] : "Stages");
    s->act_index = nf > 5 ? atoi(fld[5]) : 0;
    s->level_index = nf > 6 ? atoi(fld[6]) : (g_stage_count - 1);
    s->level_enum = nf > 7 ? atoi(fld[7]) : 0;
    s->has_prereq = nf > 8 && !mod;
    s->prereq = s->has_prereq ? atoi(fld[8]) : 0;
    if (s->character[0] >= 'a' && s->character[0] <= 'z') s->character[0] = (char)(s->character[0] - 'a' + 'A');
    int ai = -1;
    for (int i = 0; i < g_act_count; i++) {
        if (strcmp(g_act_ids[i], s->act) == 0) ai = i;
    }
    if (ai < 0 && g_act_count < MAX_ACTS && !mod) {
        snprintf(g_act_ids[g_act_count], sizeof(g_act_ids[0]), "%s", s->act);
        snprintf(g_act_names[g_act_count], sizeof(g_act_names[0]), "%s", s->act_name);
        g_act_count++;
    }
}

/* A stage index file. A missing file is normal. */
static void parse_stage_index(const char* index_path, const char* stage_root, bool mod) {
    FILE* f = fopen(index_path, "r");
    if (!f) {
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {   /* rows past MAX_STAGES are only counted (mods: the hub gauge) */
        add_stage_row(line, stage_root, mod);
    }
    fclose(f);
}

/* MOD2DS packages (MOD2DS.exe) carry no index.txt: every stage folder holds its own row in index_row.txt, so copying a package
 * onto the card never overwrites the list. Folders index.txt does not name are added here, sorted by pack, then level order. */
typedef struct { char act[24]; int level; char line[256]; } mod_row_t;
static mod_row_t g_scan_rows[MAX_STAGES];

static int mod_row_cmp(const void* a, const void* b) {
    const mod_row_t* x = (const mod_row_t*)a;
    const mod_row_t* y = (const mod_row_t*)b;
    int c = strcmp(x->act, y->act);
    return c ? c : (x->level - y->level);
}

static void scan_mod_folders(void) {
    DIR* d = opendir(MOD_ROOT);
    if (!d) {
        return;
    }
    int n = 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || strlen(e->d_name) > 31 || mod_known(e->d_name)) continue;
        char path[128];
        snprintf(path, sizeof(path), "%s/%s/index_row.txt", MOD_ROOT, e->d_name);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        char row[256];
        if (fgets(row, sizeof(row), f)) {
            char tmp[256];
            snprintf(tmp, sizeof(tmp), "%s", row);
            char* fld[BB_INDEX_FIELDS] = { 0 };
            const int nf = bb_index_split(tmp, fld, BB_INDEX_FIELDS);
            size_t idl = strlen(e->d_name);
            if (nf >= 3 && strncmp(fld[0], e->d_name, idl) == 0 && (fld[0][idl] == '\0' || fld[0][idl] == '\n' || fld[0][idl] == '\r')) {
                if (n < MAX_STAGES) {   /* the row must name its own folder: stage files are found through the id */
                    mod_row_t* r = &g_scan_rows[n];
                    snprintf(r->line, sizeof(r->line), "%s", row);
                    snprintf(r->act, sizeof(r->act), "%.23s", nf > 3 ? fld[3] : "");
                    r->level = nf > 6 ? atoi(fld[6]) : 0;
                    n++;
                } else {
                    note_mod_overflow(e->d_name);
                }
            }
        }
        fclose(f);
    }
    closedir(d);
    qsort(g_scan_rows, (size_t)n, sizeof(g_scan_rows[0]), mod_row_cmp);
    for (int i = 0; i < n; i++) add_stage_row(g_scan_rows[i].line, MOD_ROOT, true);
}

static void load_stage_index(void) {
    g_stage_count = 0;
    g_act_count = 0;
    g_mod_count = 0;
    g_mod_overflow = 0;
    parse_stage_index("romfs:/stages/index.txt", "romfs:/stages", false);
    parse_stage_index(MOD_ROOT "/index.txt", MOD_ROOT, true);
    scan_mod_folders();
    /* Level-select card: the first pre-cutscene panel in which the character speaks; two stages open on a panel without her
     * (a door sign, eyes in the dark), so they use a hand-picked panel of her instead. */
    static const struct { const char* id; int img; } k_card_pick[] = { { "claire_e1", 3 }, { "cathy_e2", 1 } };
    for (int i = 0; i < g_stage_count; i++) {
        stage_info_t* st = &g_stages[i];
        st->card_img = 0;
        char path[96], ln[320];
        if (st->mod) continue;   /* MOD2DS v1 converts no cutscenes */
        stage_path(path, sizeof(path), i, "cutscene/pre.txt");
        FILE* cf = fopen(path, "r");
        while (cf && fgets(ln, sizeof(ln), cf)) {
            if (ln[0] != 'P' || ln[1] != '|') continue;
            char* img = strchr(ln + 2, '|');
            if (!img) continue;
            char* who = img + 1;
            char* end = strchr(who, '|');
            if (!end) continue;
            *end = 0;
            if (strcasecmp(who, st->character) == 0 && atoi(ln + 2) >= 0) { st->card_img = atoi(ln + 2); break; }
        }
        if (cf) fclose(cf);
        for (size_t k = 0; k < sizeof(k_card_pick) / sizeof(k_card_pick[0]); k++) if (strcmp(st->id, k_card_pick[k].id) == 0) st->card_img = k_card_pick[k].img;
    }
}

/* The settings dialog (category grid, a page, or the credits); drawn on the top screen, or on the bottom from the pause menu. */
static void draw_settings_ui(C2D_TextBuf tb, int opt_page, int opt_cat, int settings_cursor, int credits_dir) {
    const bb_settings_t* st = &g_save.settings;
    static const char* const cats[7] = { "Audio", "Gameplay", "Controls", "Data", "Credits", "Main Menu", "Quit Game" };
    const char* labels[5];
    const char* values[5];
    char vbuf[5][32];
    if (opt_page < 0) { bb_ui_settings_menu(tb, cats, 7, opt_cat); return; }
    if (opt_page == 4) { bb_ui_credits(tb, credits_dir); return; }
    int n = 0;
    int slider[5] = { -1, -1, -1, -1, -1 };
    switch (opt_page) {
        case 0:
            labels[0] = "Master"; labels[1] = "Music"; labels[2] = "SFX"; labels[3] = "Voice";
            slider[0] = st->master_vol; slider[1] = st->music_vol; slider[2] = st->sfx_vol; slider[3] = st->voice_vol;
            for (int i = 0; i < 4; i++) vbuf[i][0] = 0;
            n = 4;
            break;
        case 1:
            labels[0] = "Calibration Menu"; labels[1] = "Unlock all stages"; labels[2] = "Cutscenes"; labels[3] = "Reduce Motion"; labels[4] = "Scroll Mode";
            snprintf(vbuf[0], sizeof(vbuf[0]), "%+d ms", st->calibration_ms);
            snprintf(vbuf[1], sizeof(vbuf[1]), "%s", st->unlock_all ? "On" : "Off");
            snprintf(vbuf[2], sizeof(vbuf[2]), "%s", st->skip_cutscene ? "Skip" : "Play");
            snprintf(vbuf[3], sizeof(vbuf[3]), "%s", st->reduce_motion ? "On" : "Off");
            snprintf(vbuf[4], sizeof(vbuf[4]), "%s", st->scroll_mode == BB_SCROLL_UP ? "Up" : (st->scroll_mode == BB_SCROLL_SIDE ? "Side" : "Down"));
            n = 5;
#if !BB_DEV_BUILD
            for (int k = 1; k < 4; k++) { labels[k] = labels[k + 1]; snprintf(vbuf[k], sizeof(vbuf[k]), "%s", vbuf[k + 1]); }   /* no unlock-all row */
            n = 4;
#endif
            break;
        case 2:
            labels[0] = "Controls";
            snprintf(vbuf[0], sizeof(vbuf[0]), "%s", k_ctrl_label[st->control_mode % BB_CTRL_COUNT]);
            n = 1;
            break;
        default:
            labels[0] = "Restore settings"; labels[1] = "Reset save data"; labels[2] = "Back";
            vbuf[0][0] = vbuf[1][0] = vbuf[2][0] = 0;
            n = 3;
            break;
    }
    for (int i = 0; i < n; i++) values[i] = vbuf[i];
    bb_ui_settings_page(tb, cats[opt_page], labels, values, slider, n, settings_cursor);
}

/* Horizontal touch rows (level portraits, power-ups): the row follows the stylus at `px` pixels per item; on release the flick speed
 * (last ~100 ms) is projected 0.22 s ahead, so a small flick moves one item and a fast one further. A tap without a drag returns the
 * zone it started and ended on (else -1). *drag_off = the row's fractional offset while dragging (0 otherwise). */
typedef struct { bool dragging, moved; int x0, cur0, zone0, hn; int hx[8]; uint64_t ht[8]; } fling_t;

static int fling_update(fling_t* f, bool tap, bool up, int x, int tap_x, int tap_y, int* cursor, int n, float px, float* drag_off) {
    int result = -1;
    *drag_off = 0.0f;
    if (n <= 0) return -1;
    if (tap) { f->dragging = true; f->moved = false; f->x0 = tap_x; f->cur0 = *cursor; f->zone0 = bb_ui_zone_hit(tap_x, tap_y); f->hn = 0; }
    if (!f->dragging) return -1;
    const int dx = x - f->x0;
    f->hx[f->hn % 8] = x; f->ht[f->hn % 8] = osGetTime(); f->hn++;
    if (!f->moved && (dx > 8 || dx < -8) && (f->zone0 < 0 || f->zone0 >= BB_ZONE_LEVEL) && f->zone0 < BB_ZONE_ACT) f->moved = true;
    if (f->moved) {
        const float steps = (float)dx / px;
        int c = f->cur0 - (int)floorf(steps + 0.5f);
        if (c < 0) c = 0;
        if (c > n - 1) c = n - 1;
        *cursor = c;
        const float off = (float)c - ((float)f->cur0 - steps);
        *drag_off = off > 0.5f ? 0.5f : (off < -0.5f ? -0.5f : off);
    }
    if (up) {
        if (f->moved) {
            float v = 0.0f;
            const int last = (f->hn - 1) % 8;
            for (int k = 1; k < 8 && k < f->hn; k++) {
                const int i = (f->hn - 1 - k) % 8;
                if (f->ht[last] - f->ht[i] >= 60 || k == 7 || k == f->hn - 1) {
                    const float dt = (float)(f->ht[last] - f->ht[i]) / 1000.0f;
                    if (dt > 0.0f) v = (float)(f->hx[last] - f->hx[i]) / dt;
                    break;
                }
            }
            const float total = (float)dx + v * 0.22f;
            int steps = (int)floorf(fabsf(total) / px + 0.5f);
            if (steps < 1) steps = 1;
            int c = f->cur0 - (total > 0.0f ? steps : -steps);
            if (c < 0) c = 0;
            if (c > n - 1) c = n - 1;
            *cursor = c;
            *drag_off = 0.0f;
        } else if (f->zone0 >= 0 && f->zone0 == bb_ui_zone_hit(tap_x, tap_y)) {
            result = f->zone0;
        }
        f->dragging = false;
    }
    return result;
}

/* Bottom-screen level select / difficulty (menu_ui.c); the data comes from the stage table and the save. */
static int level_rows(int act, int* rows) {
    int n = 0;
    for (int i = 0; i < g_stage_count; i++) if (strcmp(g_stages[i].act, g_act_ids[act]) == 0) rows[n++] = i;
    return n;
}

static void draw_levels_bottom(C2D_TextBuf tb, int cur_act, int cursor) {
    int rows[MAX_STAGES];
    bb_ui_level_t lv[MAX_STAGES];
    const int n = level_rows(cur_act, rows);
    for (int r = 0; r < n; r++) {
        const stage_info_t* si = &g_stages[rows[r]];
        const bb_stage_save_t* sv = bb_save_find_stage(&g_save, si->id);
        lv[r].id = si->id;
        lv[r].character = si->character;
        lv[r].title = si->title;
        lv[r].open = stage_open(rows[r]);
        lv[r].diffs_done = 0;
        lv[r].best_grade = 0;
        for (int d = 0; sv && d < 3; d++) if (sv->best_grade[d]) lv[r].diffs_done++;
    }
    bool owned_pu[5];
    for (int i = 0; i < 5; i++) owned_pu[i] = bb_player_has_item(&g_save.player, BB_ITEM_POWERUP_EXTRA_LIVES + i);
    bb_ui_level_select(tb, lv, n, cursor, g_act_names[cur_act], cur_act, g_act_count, (int)g_save.player.exp, owned_pu);
}

static void draw_diff_bottom(C2D_TextBuf tb, int stage, int diff) {
    const bb_stage_save_t* sv = bb_save_find_stage(&g_save, g_stages[stage].id);
    bb_ui_diff_stat_t ds[3];
    for (int d = 0; d < 3; d++) {
        ds[d].grade = sv ? sv->best_grade[d] : 0;
        ds[d].score = sv ? sv->best_score[d] : 0;
        ds[d].combo = sv ? sv->best_combo[d] : 0;
    }
    bb_ui_difficulty(tb, g_stages[stage].id, g_stages[stage].character, g_stages[stage].title, ds, diff, bb_scroll_multiplier(g_save.settings.scroll_idx));
}

static bool load_chart(int stage, int diff) {
    char rel[96], path[160];
    snprintf(rel, sizeof(rel), "charts/%s_%s.bbchart", g_stages[stage].id, k_diff_name[diff]);
    stage_path(path, sizeof(path), stage, rel);
    g_load_status = bb_chart_load_from_file(&g_chart, path);
    if (g_load_status != BB_LOAD_OK) {
        bb_chart_build_synthetic(&g_chart);
        return false;
    }
    return true;
}

/* ---------------- audio volumes (settings 0..10) ---------------- */
static void apply_volumes(void);

static void boot_intro_begin(void) {
    if (bb_theme_halloween()) {   /* the Halloween splash is drawn by boot_ui.c; its sound (splash_intro_halloween) streams here */
        g_intro_ok = false;
        bb_audio_stop_stream(&g_audio);
        bb_audio_start_stream(&g_audio, "romfs:/boot/splash_h.wav", 120000);
        apply_volumes();
        bb_diag_log("boot: halloween splash");
        return;
    }
    g_intro_ok = bb_video_init(&g_intro, "romfs:/boot/intro.bbvid", g_audio.ndsp_available, BB_VIDEO_CH);
    if (g_intro_ok) { apply_volumes(); bb_video_reset(&g_intro); }
    bb_diag_log("boot: intro ok=%d", g_intro_ok);
}

static void apply_volumes(void) {
    if (!g_audio.ndsp_available) {
        return;
    }
    const float master = (float)g_save.settings.master_vol / 10.0f;
    bb_uisfx_set_volume(master * (float)g_save.settings.sfx_vol / 10.0f);
    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = mix[1] = master * (float)g_save.settings.music_vol / 10.0f;
    ndspChnSetMix(BB_AUDIO_CHANNEL, mix);
    memset(mix, 0, sizeof(mix));
    mix[0] = mix[1] = master * (float)g_save.settings.sfx_vol / 10.0f;
    for (int ch = BB_SFX_CH_LOOP; ch <= BB_SFX_CH_UI; ch++) ndspChnSetMix(ch, mix);
    for (int i = 0; i < BB_VIDEO_CHANNELS; i++) ndspChnSetMix(BB_VIDEO_CH + i, mix);
    memset(mix, 0, sizeof(mix));
    mix[0] = mix[1] = master * (float)g_save.settings.voice_vol / 10.0f;
    ndspChnSetMix(BB_SFX_CH_VOICE, mix);   /* character voice lines have their own slider, like the original Audio > Voice */
}

static void videos_set_paused(bool paused) {
    for (int i = 0; i < g_video_count; i++) bb_video_set_paused(&g_video[i], paused);
}

static void videos_update(int64_t t) {
    for (int i = 0; i < g_video_count; i++) bb_video_update(&g_video[i], t);
}

static bool videos_all_finished(int64_t t) {
    for (int i = 0; i < g_video_count; i++) {
        if (!bb_video_finished(&g_video[i], t)) return false;
    }
    return true;
}

/* Free the per-stage assets (character sheets, sounds, videos). */
static void unload_stage_assets(void) {
    if (g_loaded_stage < 0) {
        return;
    }
    bb_diag_log("unload stage %s", g_stages[g_loaded_stage].id);
    for (int i = 0; i < g_video_count; i++) bb_video_shutdown(&g_video[i]);
    g_video_count = 0;
    bb_sfx_shutdown(&g_sfx);
    bb_anim_shutdown(&g_anim);
    g_view.video = NULL;
    g_view.video_count = 0;
    g_loaded_stage = -1;
}

static bool load_stage_assets(int stage) {
    if (g_loaded_stage == stage) {
        return true;
    }
    unload_stage_assets();
    char rel[96], path[160], gfx[160];
    uint64_t t0 = osGetTime();
    snprintf(rel, sizeof(rel), "gfx/%s.bbanim", g_stages[stage].id);
    stage_path(path, sizeof(path), stage, rel);
    stage_path(gfx, sizeof(gfx), stage, "gfx");
    bool anim_ok = bb_anim_init(&g_anim, path, gfx);
    bool all_res = anim_ok && bb_anim_load_all(&g_anim, bb_hw()->anim_resident_bytes);
    if (!g_sfx_common_tried) {   /* once, before the level bank: both set up the same NDSP channels the same way */
        g_sfx_common_tried = true;
        bb_sfx_init(&g_sfx_common, "romfs:/audio/common.bbsfx", g_audio.ndsp_available);
    }
    snprintf(rel, sizeof(rel), "sfx/%s.bbsfx", g_stages[stage].id);
    stage_path(path, sizeof(path), stage, rel);
    bool sfx_ok = bb_sfx_init(&g_sfx, path, g_audio.ndsp_available);
    g_video_count = 0;
    for (int vi = 0; vi < BB_VIDEO_MAX; vi++) {
        snprintf(rel, sizeof(rel), "video/%s_v%d.bbvid", g_stages[stage].id, vi);
        stage_path(path, sizeof(path), stage, rel);
        if (!bb_video_init(&g_video[g_video_count], path, g_audio.ndsp_available, BB_VIDEO_CH + g_video_count % BB_VIDEO_CHANNELS)) {
            break;
        }
        g_video_count++;
    }
    stage_path(path, sizeof(path), stage, "gfx/game_over.t3x");
    if (bb_theme_halloween() && !g_stages[stage].mod) {   /* GameOver.gd set_game_over_theme: game_over_halloween.png (mods keep game_over.png) */
        char hpath[160];
        stage_path(hpath, sizeof(hpath), stage, "gfx/game_over_h.t3x");
        FILE* hf = fopen(hpath, "rb");
        if (hf) { fclose(hf); snprintf(path, sizeof(path), "%s", hpath); }
    }
    bb_view_set_gameover(path);
    bb_view_init(&g_view, &g_anim);
    g_view.video = g_video_count > 0 ? g_video : NULL;
    g_view.video_count = g_video_count;
    g_loaded_stage = stage;
    apply_volumes();
    bb_diag_log("load stage %s anim=%d allres=%d sheets=%lu cams=%lu sfx=%d clips=%lu videos=%d load=%lums linear_free=%lu KiB heap_free=%lu KiB",
                g_stages[stage].id, anim_ok, all_res, (unsigned long)g_anim.sheet_count, (unsigned long)g_anim.camera_count, sfx_ok,
                (unsigned long)g_sfx.clip_count, g_video_count, (unsigned long)(osGetTime() - t0), (unsigned long)(linearSpaceFree() / 1024),
                (unsigned long)bb_hw_heap_free_kib());
    return anim_ok;
}

/* Fresh run of the selected chart: reset core + view, restart audio. */
static void save_now(const char* why);

static void start_stage(int stage, int diff, bool no_fail, bool autoplay) {
    char rel[96], wav[160];
    if (g_stages[stage].mod && bb_ach_unlock(&g_save, BB_ACH_PLAYMOD)) {   /* DifficultyMenu.gd: Trying Something Different */
        bb_uisfx_play(BB_UISFX_ACH);
        save_now("achievement");
        bb_diag_log("achievement: playing a mod");
    }
    g_chart_ok = load_chart(stage, diff);
    bb_core_init(&g_core, &g_chart);
    g_core.no_fail = no_fail;
    g_core.autoplay = autoplay;
    if (g_powerup && !bb_player_has_item(&g_save.player, g_powerup)) g_powerup = 0;   /* consumed by a pass: retry without it */
    bb_powerup_effects_t fx = bb_powerup_effects(autoplay ? 0 : g_powerup);
    g_core.score_mult_x10 = fx.score_mult_x10;
    g_core.damage = fx.damage;
    g_core.extra_health = fx.extra_health;
    if (fx.no_fail) g_core.no_fail = true;
    g_core.hardcore = fx.hardcore;
    bb_view_set_blindfold(fx.disappearing);
    g_core.calibration_offset_us = (int64_t)g_save.settings.calibration_ms * 1000;
    bb_core_restart(&g_core);
    bb_view_reset(&g_view, &g_core);
    {   /* game_outro_video.gd: the final video / audio start once the song passes the chart's last note + 0.3 s */
        const int64_t outro_us = (g_chart.note_count > 0 ? g_chart.notes[g_chart.note_count - 1].timestamp_us : 0) + 300000;
        for (int i = 0; i < g_video_count; i++) bb_video_set_outro_start(&g_video[i], outro_us);
    }
    for (int i = 0; i < g_video_count; i++) bb_video_reset(&g_video[i]);
    snprintf(rel, sizeof(rel), "audio/%s.wav", g_stages[stage].id);
    stage_path(wav, sizeof(wav), stage, rel);
    /* game_music.gd await_lead_in: the song starts after max(0, LEAD_IN_TIME 2.0 s - song_offset) of silence, and the song position
     * runs negative over it (song_position_offset = -lead_in - song_offset), so every chart opens with 2 s before its zero */
    const int64_t lead_us = 2000000LL - g_chart.song_offset_us;
    bb_audio_set_lead_in(&g_audio, g_chart_ok && lead_us > 0 ? lead_us : 0);
    bb_audio_start_stream(&g_audio, g_chart_ok ? wav : NULL, g_chart.initial_bpm_milli);
    bb_audio_set_pitch(&g_audio, 1.0f);
    bb_sfx_reset(&g_sfx);
    bb_core_start(&g_core, -g_audio.lead_in_us);
    bb_diag_log("start %s %s notes=%lu ctrl=%d cal=%dms scroll=%d linear_free=%lu KiB", g_stages[stage].id, k_diff_name[diff],
                (unsigned long)g_chart.note_count, g_save.settings.control_mode, g_save.settings.calibration_ms,
                g_save.settings.scroll_idx, (unsigned long)(linearSpaceFree() / 1024));
}

static void sfx_for_event(const bb_event_t* e) {
    switch (e->type) {
        case BB_EVT_HIT:
            /* LoopSound / Voice skip ignore_animation hits (ghost notes, hold tails) */
            if (!(e->flags & BB_EVTF_IGNORE_ANIM)) {
                bb_sfx_on_hit(&g_sfx, g_core.chart.notes[e->note_idx].timestamp_us,
                              g_core.chart.notes[e->note_idx].trigger_voice != 0);
            }
            break;
        case BB_EVT_MISS:
            if (g_core.state != BB_STATE_FAILED) play_shared_sfx("__damage");   /* Lives.DamageSound */
            break;
        case BB_EVT_ABSENT:
            play_shared_sfx("__miss");                                         /* NoteDetector.MissSound */
            break;
        default:
            break;
    }
}

/* Lane bit masks from the held keys: face buttons B/A/Y/X and D-pad Down/Right/Left/Up map to lanes 0..3. */
static uint8_t face_lanes(u32 held) {
    return (uint8_t)(((held & KEY_B) ? 1 : 0) | ((held & KEY_A) ? 2 : 0) | ((held & KEY_Y) ? 4 : 0) | ((held & KEY_X) ? 8 : 0));
}
/* Shoulders: L / ZL / ZR / R play the Y / X / B / A lanes (lanes 2 / 3 / 0 / 1); ZL and ZR are the New 2DS XL / New 3DS triggers */
static uint8_t shoulder_lanes(u32 held) {
    return (uint8_t)(((held & KEY_ZR) ? 1 : 0) | ((held & KEY_R) ? 2 : 0) | ((held & KEY_L) ? 4 : 0) | ((held & KEY_ZL) ? 8 : 0));
}
/* Book layout (Legacy mods held sideways): every button moves one place round, B -> Y, A -> B, X -> A, Y -> X, and the D-pad
 * the same way (Down -> Left, Right -> Down, Up -> Right, Left -> Up): lanes B / A / Y / X are played by Y / B / X / A. */
static uint8_t book_face_lanes(u32 held) {
    return (uint8_t)(((held & KEY_Y) ? 1 : 0) | ((held & KEY_B) ? 2 : 0) | ((held & KEY_X) ? 4 : 0) | ((held & KEY_A) ? 8 : 0));
}
static uint8_t book_dpad_lanes(u32 held) {
    return (uint8_t)(((held & KEY_DLEFT) ? 1 : 0) | ((held & KEY_DDOWN) ? 2 : 0) | ((held & KEY_DUP) ? 4 : 0) | ((held & KEY_DRIGHT) ? 8 : 0));
}
static uint8_t dpad_lanes(u32 held) {
    return (uint8_t)(((held & KEY_DDOWN) ? 1 : 0) | ((held & KEY_DRIGHT) ? 2 : 0) | ((held & KEY_DLEFT) ? 4 : 0) | ((held & KEY_DUP) ? 8 : 0));
}

static void save_now(const char* why) {
    bool ok = bb_save_io_store(&g_save);
    bb_diag_log("save (%s): %s", why, ok ? "ok" : "FAILED");
}

/* HOME Menu, lid closed (sleep) or the power menu: a running song pauses itself so the player comes back to the pause menu
 * instead of a song that resumes under their hands. The hook only raises a flag; the main loop pauses on its next frame. */
static aptHookCookie g_apt_cookie;
static volatile bool g_apt_interrupted = false;
static void apt_hook(APT_HookType hook, void* param) {
    (void)param;
    if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP) g_apt_interrupted = true;
}

/* Everything the results screen shows (flow_ui.c animates it). */
static bb_results_t g_res;

static void fill_results(int stage, int diff, int32_t pb_before, bool new_best, bool rewards, const bb_run_outcome_t* o) {
    bb_results_t* r = &g_res;
    memset(r, 0, sizeof(*r));
    snprintf(r->stage, sizeof(r->stage), "%s", g_stages[stage].title);
    snprintf(r->sub, sizeof(r->sub), "%s - %s", g_stages[stage].character, k_diff_name[diff]);
    r->sub[0] = (char)toupper((unsigned char)r->sub[0]);
    snprintf(r->grade, sizeof(r->grade), "%s", bb_grade_name(g_core.stats.grade));
    r->grade_n = (int)g_core.stats.grade;
    r->score = g_core.stats.score;
    r->pb = pb_before;
    r->accuracy = bb_core_get_accuracy_percent(&g_core.stats);
    r->perfect = g_core.stats.perfect;
    r->great = g_core.stats.great;
    r->nice = g_core.stats.nice;
    r->meh = g_core.stats.meh;
    r->miss = g_core.stats.missed + g_core.stats.absent;
    r->max_combo = g_core.stats.max_combo;
    r->hit = g_core.stats.hit;
    r->new_best = new_best;
    r->rewards = rewards;
    r->exp_total = g_save.player.exp;
    if (rewards && o) {
        r->money = o->money_gained;
        r->exp_gain = o->exp_gained;
        r->rank_before = o->rank_before;
        r->rank_after = o->rank_after;
        for (uint32_t i = 0; i < o->new_ach_count && r->toast_n < 4; i++) {
            const bb_ach_def_t* a2 = bb_ach_find(o->new_ach[i]);
            snprintf(r->toast[r->toast_n], sizeof(r->toast[0]), "%.43s", a2 ? a2->name : "Achievement");
            r->toast_kind[r->toast_n++] = 0;
        }
        for (uint32_t i = 0; i < o->new_item_count && r->toast_n < 4; i++) {
            const char* nm = NULL;
            for (int k = 0; k < g_extras.collect_count; k++) if (g_extras.collect[k].item == o->new_items[i]) nm = g_extras.collect[k].name;
            for (int k = 0; k < g_extras.gallery_count && !nm; k++) if (g_extras.gallery[k].item == o->new_items[i]) nm = g_extras.gallery[k].title;
            if (!nm) continue;
            snprintf(r->toast[r->toast_n], sizeof(r->toast[0]), "%.43s", nm);
            r->toast_kind[r->toast_n++] = 1;
        }
    }
}

/* ---------------- bottom-screen companion (companion.c): what it shows for each menu state ---------------- */
#define MAIN_ITEMS_C BB_MAIN_ITEMS   /* main-menu entries: Play, Options, Mods, Shop, Extras, Achievements, Quit */
static const char* settings_help(int opt_page, int opt_cat, int row) {
    static const char* const cat_help[7] = {
        "Master, music, sound effect and voice volume.",
        "Calibration, cutscenes, unlocks and Reduce Motion.",
        "Play with the face buttons, the D-Pad or both.",
        "Restore the default settings or erase your save.",
        "The people who made Beat Banger.",
        "Save your settings and go back to the main menu.",
        "Save your settings and return to the HOME Menu.",
    };
    if (opt_page < 0) return cat_help[opt_cat < 0 ? 0 : (opt_cat > 6 ? 6 : opt_cat)];
    switch (opt_page * 10 + row) {
        case 0:  return "Overall volume of everything.";
        case 1:  return "Songs and menu music.";
        case 2:  return "Hit sounds, menu sounds and effects.";
        case 3:  return "Character voice lines in levels and cutscenes.";
        case 10: return "If notes feel early or late, open the Calibration Menu. Results also suggest a value after each level.";
        case 11: return "Open every level without clearing the ones before it.";
        case 12: return "Play or skip the scenes before and after each level.";
        case 13: return "Fewer slides, shakes and sparkles in the menus. Gameplay is unchanged.";
        case 14: return "Down: notes fall to the bottom. Up: notes rise to the top. Side: notes slide in from the right. Four lanes in every mode.";
        case 20: return bb_hw()->has_zlzr ? "Buttons: B A Y X. D-Pad: Down Right Left Up. Hybrid: either one, both together count once. Shoulders: L ZL ZR R."
                                    : "Buttons: B A Y X. D-Pad: Down Right Left Up. Hybrid: either one; pressing both together counts once.";
        case 30: return "Back to the default settings. Scores, progress and purchases stay.";
        case 31: return "Erase scores, progress, purchases and settings.";
        case 32: return "Return to the settings categories.";
        default: return opt_page == 4 ? "Credits of the original game." : "";
    }
}

static void fill_companion(bb_comp_t* c, app_state_t state, int cursor, int cur_act, int stage, int diff, int opt_page, int opt_cat,
                           int settings_cursor, bool no_hud, bool no_beat_bar, bool music_ok) {
    switch (state) {
        case APP_TITLE:
            c->kind = BB_COMP_TITLE;
            bb_comp_hint(c, "A", "Start");
            break;
        case APP_MENU_SETTINGS:
            c->kind = BB_COMP_HELP;
            c->help = settings_help(opt_page, opt_cat, settings_cursor);
            if (opt_page == 4) { bb_comp_hint(c, "D-Pad", "Scroll"); bb_comp_hint(c, "B", "Back"); }
            else if (opt_page >= 0) { bb_comp_hint(c, "A", "Change"); bb_comp_hint(c, "L/R", "Page"); bb_comp_hint(c, "B", "Back"); }
            else { bb_comp_hint(c, "A", "Select"); bb_comp_hint(c, "B", "Back"); }
            break;
        case APP_MENU_CONFIRM_RESET:
            c->kind = BB_COMP_HELP;
            c->help = "Scores, progress, purchases and settings will be erased. This cannot be undone.";
            bb_comp_hint(c, "A", "Confirm");
            bb_comp_hint(c, "B", "Cancel");
            break;
        case APP_CALIB:
            c->kind = BB_COMP_HELP;
            c->help = "Change the offset until the circles flash exactly on the beat you hear.";
            bb_comp_hint(c, "D-Pad", "Adjust");
            bb_comp_hint(c, "B", "Back");
            break;
        case APP_SCREEN:
            switch (g_scr.node) {
                case SCN_EXTRAS: bb_comp_hint(c, "A", "Select"); bb_comp_hint(c, "B", "Back"); break;
                case SCN_THEATER: bb_comp_hint(c, "A", "Watch"); bb_comp_hint(c, "D-Pad", "Reel"); bb_comp_hint(c, "B", "Back"); break;
                case SCN_CHARS: bb_comp_hint(c, "L/R", "Character"); bb_comp_hint(c, "B", "Back"); break;
                case SCN_COLLECT: bb_comp_hint(c, "L/R", "Page"); bb_comp_hint(c, "B", "Back"); break;
                case SCN_GALLERY: bb_comp_hint(c, "A", "View"); bb_comp_hint(c, "B", "Back"); break;
                case SCN_GALLERY_VIEW: bb_comp_hint(c, "L/R", "Next"); bb_comp_hint(c, "B", "Back"); break;
                case SCN_POWERUP: bb_comp_hint(c, "A", "Play"); bb_comp_hint(c, "B", "Back"); break;
                default: bb_comp_hint(c, "B", "Close"); break;
            }
            break;
        default:
            break;
    }
}

/* A mod may only add a save record while every built-in stage / volume still has a slot left for its own (bb_save_room_for_extra);
 * a mod that is already recorded just updates its record. */
static bool mod_record_room(void) {
    const char* bs[MAX_STAGES];
    const char* ba[MAX_ACTS];
    int ns = 0;
    for (int i = 0; i < g_stage_count; i++) if (!g_stages[i].mod) bs[ns++] = g_stages[i].id;
    for (int i = 0; i < g_act_count; i++) ba[i] = g_act_ids[i];
    return bb_save_room_for_extra(&g_save, bs, ns, ba, g_act_count);
}

/* Smoke: one line with the progression state as a player sees it (levels open, economy, Extras unlock counts). */
static void progress_log(const char* tag) {
    char open[160] = "";
    for (int i = 0; i < g_stage_count; i++) {
        if (g_stages[i].mod) continue;
        char b[40];
        snprintf(b, sizeof(b), "%s%.24s:%c", open[0] ? " " : "", g_stages[i].id, stage_open(i) ? 'O' : 'L');
        strncat(open, b, sizeof(open) - strlen(open) - 1);
    }
    const bb_player_t* p = &g_save.player;
    int sv = 0, reels = 0, recs = 0, coll = 0;
    static const int k_lv_items[8] = { 501, 502, 503, 504, 505, 506, 507, 598 };
    for (int k = 0; k < 8; k++) if (bb_player_has_item(p, k_lv_items[k])) sv++;
    for (int k = 0; k < g_scr.reel_stages * 2; k++) if (bb_screens_reel_open(&g_scr, k)) reels++;
    static const int k_records[10] = { 201, 202, 203, 204, 205, 206, 207, 297, 298, 299 };
    for (int k = 0; k < 10; k++) if (bb_player_has_item(p, k_records[k])) recs++;
    for (int k = 0; k < g_extras.collect_count; k++) if (bb_player_has_item(p, g_extras.collect[k].item)) coll++;
    int gown = 0, gtot = 0;
    bb_gal_counts(&g_save, &gown, &gtot);
    bb_diag_log("progress %s: unlock_all=%d money=%ld exp=%ld rank=%s beaten=%ld ach=%u items=%u | %s | dormkey=%d egg901=%d | theater=%d/%d sceneviewer=%d/8 "
                "records=%d/10 collection=%d/%d gallery=%d/%d", tag, g_save.settings.unlock_all, (long)p->money, (long)p->exp,
                bb_rank_title(bb_rank_from_exp(p->exp)), (long)p->stats[BB_STAT_LEVELS_BEATEN], (unsigned)p->ach_count, (unsigned)p->item_count, open,
                bb_player_has_item(p, BB_ITEM_COLLECT_DORM_KEY), bb_player_has_item(p, BB_ITEM_EGG_DORM_KEY_READ), reels, g_scr.reel_stages * 2, sv,
                recs, coll, g_extras.collect_count, gown, gtot);
}

/* Mods > Library rows (bb_mod_entry_t points into g_stages; the thumbnail paths live here) */
static bb_mod_entry_t g_mod_entries[MAX_STAGES];
static char g_mod_thumb[MAX_STAGES][96];
static bool g_mods_open = false;

static void mods_refresh(void) {
    for (int k = 0; k < g_mod_count; k++) {
        const int i = g_mod_rows[k];
        bb_mod_entry_t* e = &g_mod_entries[k];
        e->title = g_stages[i].title;
        e->character = g_stages[i].character;
        e->pack = g_stages[i].act_name;
        stage_path(g_mod_thumb[k], sizeof(g_mod_thumb[k]), i, "menu/thumb.t3x");
        e->thumb_path = g_mod_thumb[k];
        e->best_grade = 0;
        e->best_score = 0;
        const bb_stage_save_t* sv = bb_save_find_stage(&g_save, g_stages[i].id);
        for (int d = 0; sv && d < 3; d++) {
            if (sv->best_grade[d] > e->best_grade) e->best_grade = sv->best_grade[d];
            if (sv->best_score[d] > e->best_score) e->best_score = sv->best_score[d];
        }
        char cp[96];
        stage_path(cp, sizeof(cp), i, "cutscene/pre.txt");
        FILE* cf = fopen(cp, "r");
        e->has_cutscenes = cf != NULL;
        if (cf) fclose(cf);
    }
    if (!g_mods_open) { bb_mods_open(g_mod_entries, g_mod_count); g_mods_open = true; }
}

static void mods_release(void) {
    if (g_mods_open) { bb_mods_close(); g_mods_open = false; }
}

/* Open stage `stage`'s pre/post cutscene with the current volumes; the stage assets are freed first (the scene needs the linear memory). */
static bool g_cs_noaudio = false;   /* smoke debugging: "cutscene <stage> noaudio" */
static bool open_cutscene(int stage, const char* which) {
    char dir[64];
    snprintf(dir, sizeof(dir), "%s/%s", g_stages[stage].root, g_stages[stage].id);
    unload_stage_assets();
    bb_audio_stop_stream(&g_audio);
    if (!bb_cs_open(dir, which, g_audio.ndsp_available && !g_cs_noaudio)) {
        return false;
    }
    const bb_settings_t* st = &g_save.settings;
    bb_cs_set_volumes((float)st->master_vol / 10.0f, (float)st->music_vol / 10.0f, (float)st->sfx_vol / 10.0f, (float)st->voice_vol / 10.0f);
    bb_diag_log("cutscene %s/%s open panels=%d", g_stages[stage].id, which, bb_cs_panel_count());
    return true;
}

/* TEST BUILD: everything unlocked for testing (user request): every level (unlock_all), every shop item (photos, music, outfits,
 * one of each power-up), every collectable and gallery picture. Granted into the loaded save, so it sticks once the game saves;
 * set BB_TEST_UNLOCK_EVERYTHING to 0 for a normal build. Achievements are not granted (they are still earned). */
#ifndef BB_DEV_BUILD
#define BB_DEV_BUILD 0   /* `make DEV=1`: the unlock-all switch in Settings > Gameplay and the boot-time unlock below */
#endif
#ifndef BB_TEST_UNLOCK_EVERYTHING
#define BB_TEST_UNLOCK_EVERYTHING BB_DEV_BUILD   /* never in a normal build: a player's save progresses naturally */
#endif
#if BB_TEST_UNLOCK_EVERYTHING
static void test_unlock_everything(void) {
    bb_player_t* p = &g_save.player;
    int n = 0;
    g_save.settings.unlock_all = 1;
    for (int i = 0; i < g_extras.shop_count; i++) n += bb_player_add_item(p, g_extras.shop[i].item);
    for (int i = 0; i < g_extras.collect_count; i++) n += bb_player_add_item(p, g_extras.collect[i].item);
    for (int i = 0; i < g_extras.gallery_count; i++) if (g_extras.gallery[i].item) n += bb_player_add_item(p, g_extras.gallery[i].item);
    bb_diag_log("TEST BUILD: everything unlocked (%d items granted)", n);
}
#endif

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    bb_hw_init();   /* console profile: CPU mode (804 MHz + L2 on New models), loader core, budgets */
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    char smoke_line[64];
    bb_diag_init(smoke_line, sizeof(smoke_line));
    char smoke_mode[16] = "", smoke_stage[32] = "", smoke_diff[16] = "";
    sscanf(smoke_line, "%15s %31s %15s", smoke_mode, smoke_stage, smoke_diff);
    const bool showcase = strcmp(smoke_mode, "showcase") == 0;   /* a scripted ~5 min tour of the real game for a recording (not a smoke test) */
    if (showcase) smoke_mode[0] = '\0';
    bool smoke = smoke_mode[0] != '\0';
    if (smoke) bb_hw_simulate_old3ds_memory();   /* emulator smoke runs: the real Old 3DS heap size, before anything is loaded */
    bool smoke_fail = strcmp(smoke_mode, "fail") == 0;       /* no input: exercise the fail / game-over path */
    bool smoke_auto = strcmp(smoke_mode, "autoplay") == 0;
    bool smoke_stress = strcmp(smoke_mode, "stress") == 0;   /* mash B, die, retry x N, then give up */
    bool smoke_settings = strcmp(smoke_mode, "settings") == 0; /* change + store settings, then quit */
    bool smoke_menus = strcmp(smoke_mode, "menus") == 0;     /* capture each menu screen, then quit */
    bool smoke_record = strcmp(smoke_mode, "record") == 0;   /* store sample results + progression, then quit */
    bool smoke_calib = strcmp(smoke_mode, "calib") == 0;    /* open the calibration menu, capture, quit */
    bool smoke_shopflow = strcmp(smoke_mode, "shopflow") == 0; /* scripted button presses through the real shop code (buy, talk, outfits) */
    bool smoke_preview = strcmp(smoke_mode, "preview") == 0;  /* level select: highlight levels, check the preview streams */
    bool smoke_touch = strcmp(smoke_mode, "touch") == 0;     /* scripted taps: main menu select/open, level select, difficulty, modifiers */
    bool smoke_egg = strcmp(smoke_mode, "egg") == 0;         /* main menu: touch the easter egg five times, then touch Options */
    bool smoke_soundtest = strcmp(smoke_mode, "soundtest") == 0;  /* Extras > Music: play a record, captures */
    bool smoke_sceneview = strcmp(smoke_mode, "sceneview") == 0;  /* Extras > Scene Viewer: load a level, step loops, a song */
    bool smoke_theme = strcmp(smoke_mode, "theme") == 0;      /* Change Theme: Halloween splash + menu, then back to the default */
    bool smoke_boop = strcmp(smoke_mode, "boop") == 0;        /* Shop > Boop: eight boops on Jasmine's nose (lines at 2, 5, the photo at 7) */
    bool smoke_gallery = strcmp(smoke_mode, "gallery") == 0;  /* Extras > Gallery: grid, tab switch, lightbox, zoom in x2, close */
    bool smoke_bios = strcmp(smoke_mode, "bios") == 0;        /* Extras > Characters: tap the character twice (clothed -> lingerie -> nude) */
    bool smoke_boot = strcmp(smoke_mode, "boot") == 0;
    bool smoke_warnegg = strcmp(smoke_mode, "warnegg") == 0;   /* the warning screen's B easter egg: captures, then the game exits itself */        /* the boot sequence: warning, disclaimer, intro, main menu */
    bool smoke_modsui = strcmp(smoke_mode, "modsui") == 0;    /* Main menu > Mods: hub, library, a mod's screen, its difficulty, back */
    bool smoke_progress = strcmp(smoke_mode, "progress") == 0;   /* "progress <stage> [diff]": a real, rewarded pass from the current save (no screenshots) */
    bool smoke_readkey = strcmp(smoke_mode, "readkey") == 0;     /* Extras > Collection: read Cathy's Dorm Key, check After Class opened */
    bool smoke_collect = strcmp(smoke_mode, "collect") == 0 || smoke_modsui || smoke_boot || smoke_bios || smoke_gallery || smoke_boop || smoke_soundtest || smoke_sceneview || smoke_theme || smoke_warnegg || smoke_readkey;  /* Extras > Collection: the book, the popup (mid / open), close, a page turn */
    bool smoke_uiperf = strcmp(smoke_mode, "uiperf") == 0;   /* ~3 s on each main menu / shop / extras screen, no captures: main-thread time per screen */
    bool smoke_screens = strcmp(smoke_mode, "screens") == 0 || smoke_uiperf || smoke_collect || smoke_shopflow || smoke_calib || smoke_preview || smoke_egg || smoke_touch; /* seed an economy, capture every shop/extras screen, then quit */
    u32 drive_key = 0;
    g_cs_noaudio = strcmp(smoke_diff, "noaudio") == 0;
    if (strcmp(smoke_diff, "auto") == 0) bb_cs_set_autoplay(true);   /* smoke: the scene plays itself */
    bool smoke_flow = strcmp(smoke_mode, "flow") == 0;       /* pre cutscene, then the stage itself for ~8 s */
    bool smoke_cutscene = strcmp(smoke_mode, "cutscene") == 0 || smoke_flow; /* step through a stage pre and post cutscenes, capturing panels */
    bool smoke_psettings = strcmp(smoke_mode, "pausesettings") == 0;   /* pause -> Settings (bottom screen) -> Audio page -> back to the pause menu */
    bool smoke_pause = strcmp(smoke_mode, "pause") == 0;     /* play ~6 s, open the pause menu, capture it, quit */
    bool smoke_music = strcmp(smoke_mode, "music") == 0;     /* scene music in the main menu and shop, then quit */
    bool smoke_scroll = strcmp(smoke_mode, "scroll") == 0;   /* "scroll <stage> up|side|down": a short run in that Scroll Mode (not saved) */
    bool smoke_short = strcmp(smoke_mode, "short") == 0 || smoke_scroll;   /* play ~14 s of a stage then quit (new-content smoke) */
    bool smoke_results = strcmp(smoke_mode, "results") == 0;
    bool smoke_perf = strcmp(smoke_mode, "perf") == 0;       /* "perf <stage> <diff>": the whole stage on autoplay, no screenshots, every slow frame logged */
    bb_diag_set_sync(smoke && !smoke_perf);   /* players (and perf, which measures their setup): telemetry stays in RAM during a song, written in the menus */
    uint32_t perf_frames = 0, perf_over20 = 0, perf_over34 = 0, perf_logged = 0;
    double perf_worst = 0.0, perf_worst_at = 0.0;
    double last_busy_ms = 0.0, wait_ms = 0.0, busy_avg = 0.0, busy_win_max = 0.0, busy_perf_sum = 0.0, busy_perf_max = 0.0;   /* main thread: frame time minus the vsync wait */
    uint32_t deaths = 0;

    Result romfs_rc = romfsInit();
    bool romfs_ok = R_SUCCEEDED(romfs_rc);
    load_stage_index();
    bool save_loaded = bb_save_io_load(&g_save);
#if !BB_DEV_BUILD
    g_save.settings.unlock_all = 0;   /* a test build's unlock-all switch never carries into a normal build (progress is untouched) */
#endif
    bb_theme_set(g_save.settings.theme);   /* Calendar.GAME_THEME, chosen with the Change Theme button */
    if (smoke_scroll) g_save.settings.scroll_mode = strcmp(smoke_diff, "up") == 0 ? BB_SCROLL_UP : (strcmp(smoke_diff, "side") == 0 ? BB_SCROLL_SIDE : BB_SCROLL_DOWN);
    bb_ui_set_reduce_motion(g_save.settings.reduce_motion != 0);

    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    C2D_TextBuf text_buf = C2D_TextBufNew(4096);

    bool audio_ok = bb_audio_init(&g_audio);
    bool ui_ok = bb_view_ui_init();
    bb_art_set_theme(bb_theme_halloween());   /* before bb_art_init: the theme's logo and menu loop are the first ones loaded */
    bool art_ok = bb_art_init();
    bool font_ok = bb_font_init();
    bool uisfx_ok = bb_uisfx_init(g_audio.ndsp_available);
    apply_volumes();
    aptHook(&g_apt_cookie, apt_hook, NULL);
    bb_diag_log("=== Beat Banger 3DS runtime log (new session) ===");
    bb_diag_log("romfs=%d stages=%d acts=%d audio_init=%d ndsp=%d ui=%d art=%d font=%d uisfx=%d smoke='%s' linear_free=%lu KiB", romfs_ok, g_stage_count, g_act_count,
                audio_ok, g_audio.ndsp_available, ui_ok, art_ok, font_ok, uisfx_ok, smoke_line, (unsigned long)(linearSpaceFree() / 1024));
    {
        char hw_line[320];
        bb_hw_describe(hw_line, sizeof(hw_line));
        bb_diag_log("%s", hw_line);
    }
    if (!bb_input_mode_available((bb_control_mode_t)g_save.settings.control_mode, bb_hw()->has_zlzr)) {   /* SD card moved from a New 3DS */
        bb_diag_log("controls: %d needs ZL/ZR, this console has none -> Buttons", g_save.settings.control_mode);
        g_save.settings.control_mode = (uint8_t)bb_input_mode_effective((bb_control_mode_t)g_save.settings.control_mode, bb_hw()->has_zlzr);
    }
    bb_diag_log("save loaded=%d ctrl=%d cal=%dms scroll=%d music=%d sfx=%d unlock_all=%d stages_saved=%lu", save_loaded, g_save.settings.control_mode,
                g_save.settings.calibration_ms, g_save.settings.scroll_idx, g_save.settings.music_vol, g_save.settings.sfx_vol,
                g_save.settings.unlock_all, (unsigned long)g_save.stage_count);

    if (smoke_settings) {
        g_save.settings.control_mode = BB_CTRL_HYBRID;
        g_save.settings.calibration_ms = 15;
        g_save.settings.scroll_idx = 2;
        g_save.settings.music_vol = 8;
        save_now("smoke settings");
        goto quit;
    }

    if (smoke_record) {
        bb_save_record_result(&g_save, "cathy_e1", 0, 25844, (uint8_t)BB_GRADE_S, 288);
        bb_save_record_result(&g_save, "cathy_e1", 2, 58630, (uint8_t)BB_GRADE_SS, 569);
        bb_save_record_result(&g_save, "cathy_e1", 0, 100, (uint8_t)BB_GRADE_B, 5); /* worse: must not overwrite */
        bb_save_complete_level(&g_save, "bb_vol_1", 0);
        save_now("smoke record");
        goto quit;
    }

    if (g_stage_count == 0) {
        goto quit;
    }

    int stage = 0, diff = 0;
    for (int i = 0; i < g_stage_count; i++) {
        if (smoke_stage[0] && strcmp(smoke_stage, g_stages[i].id) == 0) stage = i;
    }
    for (int i = 0; i < 3; i++) {
        if (smoke_diff[0] && strcmp(smoke_diff, k_diff_name[i]) == 0) diff = i;
    }
    if (strcmp(smoke_mode, "normal") == 0) diff = 1;
    if (strcmp(smoke_mode, "expert") == 0) diff = 2;
    bool opt_no_fail = smoke && !smoke_fail && !smoke_stress && !smoke_progress;
    bool opt_autoplay = smoke_auto || smoke_perf;

    g_chart_ok = load_chart(stage, diff);
    bb_core_init(&g_core, &g_chart);
    bb_view_init(&g_view, &g_anim);

    app_state_t state = APP_MENU_MAIN;
    uint64_t state_t0 = osGetTime();
    uint64_t state_ms = 0;
    int64_t audio_pos_us = 0;
    int64_t calib_pos_us = 0;                /* scene-music stream position (calibration beat clock) */
    int64_t outro_base_us = 0;
    uint32_t caption = 0;
    int cursor = 0, cur_act = 0, pause_cursor = 0, settings_cursor = 0, confirm_cursor = 0, opt_page = -1, opt_cat = 0, mod_cursor = 0;
    bool mod_no_hud = false, mod_no_beat_bar = false, mod_legacy_layout = true;
    bool want_launch = false, screen_from_main = true, menu_music_ok = false, launch_skip_cs = false, launch_go = false;
    uint64_t launch_t0 = 0;
    int cs_after = 0;                       /* 0 = launch the stage afterwards, 1 = back to the level list, 2/3 = smoke (pre/post), 4 = back to the Theater */
    int launch_kind = 0, theater_stage = -1, theater_post = 0;
    int results_focus = 0;                  /* results buttons: 0 Continue, 1 Retry (UiButton focus) */
    int fail_focus = 0;                     /* level failed buttons: 0 Retry, 1 Give Up */
    bool settings_from_pause = false;       /* Settings opened from the pause menu: B returns to the paused level */
    uint64_t play_fade_t0 = 0;              /* when the stage appeared after the loading screen (fade from black) */
#define LOAD_OUT_MS 700                     /* loading screen: fade in 0.28 s, hold, fade out from 0.7 s for 0.25 s */
#define LOAD_FADE_MS 250
    int smoke_tx = -1, smoke_ty = -1;       /* smoke: one injected touch on the bottom screen */   /* launch_kind 1: the dip ends in a Theater cutscene instead of a stage */
    int smoke_cs_adv = 0, smoke_cs_cap = -1;
    int main_cursor = 0;
    int boot_step = 0;                         /* boot sequence screen (boot_ui.h) */
    uint64_t boot_leave = 0;                   /* when the current boot screen started fading out (0 = not leaving) */
    int mm_page = 0, mm_hub = 0, mm_lib = 0;   /* Mods: 0 hub, 1 library, 2 a mod's screen; hub and library cursors */
    bool mod_flow = false;                     /* the difficulty / level came from Mods: every way back returns to the mod's screen */
    bool mm_armed = false;                     /* a mod's screen: the first tap on the portrait arms it, the second opens */
    int mods_return = 0;                       /* modifiers opened from: 0 difficulty, 1 the Mods hub, 2 a mod's screen */
    bb_run_outcome_t outcome;
    memset(&outcome, 0, sizeof(outcome));
    uint8_t prev_lanes = 0;
    bool show_diag = false;

    uint64_t last_tick = svcGetSystemTick();
    double frame_avg = 16.67, frame_max = 0.0;
    uint32_t frame_count = 0;
    uint32_t smoke_cursor = 0;
    int64_t prev_audio_us = 0;
    double astep_max_ms = 0.0;
    uint32_t astall_frames = 0, aplay_frames = 0;
    bool snap_a = false, snap_b = false, snap_c = false, snap_d = false, snap_v = false;
    bb_diag_snapshot_t snap;
    char diag_lines[12][64];
    int diag_n = 0;
    memset(&snap, 0, sizeof(snap));

    hidSetRepeatParameters(20, 4);

#define ENTER(s) do { state = (s); state_t0 = osGetTime(); state_ms = 0; bb_ui_enter(); } while (0)
    /* "back to the level list": the volumes, or the mod's own screen when the level came from Mods */
#define ENTER_LEVELS() do { if (mod_flow) { mods_refresh(); mm_page = 2; mm_armed = false; ENTER(APP_MODMGR); } else ENTER(APP_MENU_LEVELS); } while (0)
#define MAIN_ITEMS MAIN_ITEMS_C

    bool extras_ok = romfs_ok && bb_extras_load(&g_extras);
    bb_screens_init(&g_scr, &g_extras, &g_save);
    for (int i = 0; i < g_stage_count && g_scr.level_count < 16; i++) {
        if (g_stages[i].level_enum) g_scr.level_enums[g_scr.level_count++] = g_stages[i].level_enum;
    }
    for (int i = 0; i < g_stage_count; i++) {   /* Extras > Theater: every stage that ships its pre / post cutscenes */
        char cp[96];
        if (g_stages[i].mod) continue;
        stage_path(cp, sizeof(cp), i, "cutscene/pre.txt");
        FILE* cf = fopen(cp, "r");
        if (!cf) continue;
        fclose(cf);
        bb_screens_add_reel_stage(&g_scr, g_stages[i].id, g_stages[i].character, g_stages[i].title, g_stages[i].act, g_stages[i].level_enum);
    }
    bb_diag_log("extras loaded=%d shop=%d collect=%d lore=%d gallery=%d dlg=%d talk=%d money=%ld exp=%ld", extras_ok, g_extras.shop_count,
                g_extras.collect_count, g_extras.lore_count, g_extras.gallery_count, g_extras.dlg_count, g_extras.talk_count,
                (long)g_save.player.money, (long)g_save.player.exp);
#if BB_TEST_UNLOCK_EVERYTHING
    if (extras_ok) test_unlock_everything();
#endif

    ENTER((smoke && !smoke_menus && !smoke_screens && !smoke_music && !smoke_cutscene) ? APP_MENU_DIFF : ((!smoke || smoke_menus || smoke_boot || smoke_warnegg) ? APP_TITLE : APP_MENU_MAIN));
    if (smoke_cutscene) {
        if (open_cutscene(stage, "pre")) { cs_after = smoke_flow ? 0 : 2; ENTER(APP_CUTSCENE); }
        else { bb_diag_log("SMOKE END cutscene (no pre)"); goto quit; }
    }
    if (smoke_progress) {
        progress_log("before");
        if (!stage_open(stage)) { bb_diag_log("SMOKE END progress %s LOCKED", g_stages[stage].id); goto quit; }
        bb_diag_log("progress: %s is open, playing it (diff %d)", g_stages[stage].id, diff);
    }
    if (smoke && !smoke_menus && !smoke_screens && !smoke_music && !smoke_cutscene) {
        cur_act = 0;
        for (int i = 0; i < g_act_count; i++) if (strcmp(g_act_ids[i], g_stages[stage].act) == 0) cur_act = i;
    }
    if (smoke_results) {   /* results screen with sample numbers over a loaded stage ("results <stage> low": the "break" path) */
        const bool low = strcmp(smoke_diff, "low") == 0;
        load_stage_assets(stage);
        start_stage(stage, 1, true, false);
        bb_audio_stop_stream(&g_audio);
        g_core.stats.registered = 337;
        g_core.stats.hit = low ? 219 : 335;
        g_core.stats.max_combo = low ? 41 : 214;
        g_core.stats.perfect = low ? 120 : 300;
        g_core.stats.great = low ? 60 : 30;
        g_core.stats.nice = low ? 39 : 5;
        g_core.stats.missed = 337 - g_core.stats.hit;
        g_core.stats.score = low ? 14250 : 31480;
        g_core.stats.grade = low ? BB_GRADE_D : BB_GRADE_A;
        fill_results(stage, 1, 25844, false, false, NULL);
        g_res.rewards = true;   /* sample banners: payout, then one trophy */
        g_res.money = 120;
        g_res.exp_gain = 45;
        snprintf(g_res.toast[0], sizeof(g_res.toast[0]), "Taste Of Victory");
        g_res.toast_kind[0] = 0;
        g_res.toast_n = 1;
        bb_results_begin();
        results_focus = 0;
        ENTER(APP_RESULTS);
    }

    while (aptMainLoop()) {
        uint64_t now_tick = svcGetSystemTick();
        double dt_ms = (double)(now_tick - last_tick) / ((double)SYSCLOCK_ARM11 / 1000.0);
        last_tick = now_tick;
        if (frame_count > 5) {
            frame_avg = frame_avg * 0.98 + dt_ms * 0.02;
            if (dt_ms > frame_max) frame_max = dt_ms;
            double busy_ms = dt_ms - wait_ms;
            last_busy_ms = busy_ms;
            busy_avg = busy_avg * 0.98 + busy_ms * 0.02;
            if (busy_ms > busy_win_max) busy_win_max = busy_ms;
            if (smoke_perf && state == APP_PLAYING && state_ms > 1000) {
                busy_perf_sum += busy_ms;
                if (busy_ms > busy_perf_max) busy_perf_max = busy_ms;
            }
        }
        frame_count++;

        /* Sample the audio clock immediately before reading input so presses use the freshest position */
        if (state == APP_PLAYING || state == APP_FAIL_TAPE || state == APP_OUTRO || state == APP_PAUSED || state == APP_COUNTIN) {
            audio_pos_us = bb_audio_update(&g_audio);
            if (state == APP_PLAYING && g_audio.ndsp_available && frame_count > 120) {
                double step_ms = (double)(audio_pos_us - prev_audio_us) / 1000.0;
                if (step_ms > astep_max_ms) astep_max_ms = step_ms;
                if (audio_pos_us == prev_audio_us) astall_frames++;
                aplay_frames++;
            }
            prev_audio_us = audio_pos_us;
        }
        /* perf smoke: every frame of play after the fade-in; a frame over 20 ms is a visible hitch at 60 Hz */
        if (smoke_perf && state == APP_PLAYING && state_ms > 1000) {
            perf_frames++;
            if (dt_ms > 20.0) {
                perf_over20++;
                if (dt_ms > 34.0) perf_over34++;
                if (dt_ms > perf_worst) { perf_worst = dt_ms; perf_worst_at = (double)g_core.song_cur_time_us / 1e6; }
                if (perf_logged++ < 40)
                    bb_diag_log("PERF slow frame %.1fms chart=%+.2fs sheet_loads=%lu lastload=%lums und=%lu", dt_ms, (double)g_core.song_cur_time_us / 1e6,
                                (unsigned long)g_anim.loads, (unsigned long)g_anim.last_load_ms, (unsigned long)g_audio.underrun_count);
            }
        }
        if (state != APP_PLAYING && state != APP_OUTRO && state != APP_COUNTIN && state != APP_FAIL_TAPE) bb_diag_flush();
        bb_ui_tick((float)dt_ms / 1000.0f);
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();
        u32 kRep = hidKeysDownRepeat();
        /* Touch (bottom screen): tap = the stylus went down this frame; touch_up = it was lifted (last position in touch_x/y) */
        static int touch_x = 0, touch_y = 0;
        bool tap = false, touch_up = (hidKeysUp() & KEY_TOUCH) != 0;
        if (kHeld & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            touch_x = tp.px;
            touch_y = tp.py;
        }
        if (kDown & KEY_TOUCH) tap = true;
        if (smoke_tx >= 0) { tap = touch_up = true; touch_x = smoke_tx; touch_y = smoke_ty; smoke_tx = smoke_ty = -1; }   /* smoke: a tap = down + up */
        const int tap_x = touch_x, tap_y = touch_y;
        /* the circle pad works as the D-pad in the menus (the level list maps it itself: it scrolls but never changes the volume) */
        if (state == APP_MENU_SETTINGS || state == APP_MENU_CONFIRM_RESET || state == APP_MENU_DIFF || state == APP_MENU_MODS ||
            state == APP_PAUSED || state == APP_RESULTS || state == APP_CALIB || state == APP_CUTSCENE) {
            static const u32 cpad[4][2] = { { KEY_CPAD_UP, KEY_DUP }, { KEY_CPAD_DOWN, KEY_DDOWN }, { KEY_CPAD_LEFT, KEY_DLEFT }, { KEY_CPAD_RIGHT, KEY_DRIGHT } };
            for (int k = 0; k < 4; k++) {
                if (kDown & cpad[k][0]) kDown |= cpad[k][1];
                if (kRep & cpad[k][0]) kRep |= cpad[k][1];
            }
        }

        if (g_egg_on) { kDown = kRep = 0; kHeld &= KEY_TOUCH; tap = false; }   /* the easter egg: every button is ignored until it ends */
        if ((kDown & KEY_START) && (kHeld & KEY_SELECT)) {
            break;
        }
        if ((kHeld & KEY_L) && (kHeld & KEY_R) && (kDown & KEY_Y)) {
            show_diag = !show_diag;
        }
        state_ms = osGetTime() - state_t0;

        if (launch_go) { kDown = kRep = 0; tap = touch_up = false; }   /* the launch fade swallows input */
        if (g_apt_interrupted) {               /* back from the HOME Menu / sleep: a running song waits in the pause menu */
            g_apt_interrupted = false;
            if (state == APP_PLAYING) {
                bb_core_pause(&g_core);
                bb_audio_set_paused(&g_audio, true);
                videos_set_paused(true);
            }
            if (state == APP_PLAYING || state == APP_COUNTIN) {
                ENTER(APP_PAUSED);
                pause_cursor = 0;
                kDown = kRep = 0;
                bb_diag_log("auto-pause (HOME Menu / sleep) chart=%+.2fs", (double)g_core.song_cur_time_us / 1e6);
            }
        }
        if ((smoke_shopflow || smoke_pause || smoke_psettings || smoke_collect || showcase) && drive_key) { kDown |= drive_key; kRep |= drive_key; drive_key = 0; }

        /* Per-frame lane state for gameplay: both input groups feed one combined mask, so equivalent inputs
         * pressed together are one press and a hold ends only when both are released (core/input_map.h). */
        bb_control_mode_t ctrl = (bb_control_mode_t)g_save.settings.control_mode;
        const bool book_keys = mod_legacy_layout && stage_is_legacy(stage);
        uint8_t lanes = bb_input_lanes_ex(ctrl, book_keys ? book_face_lanes(kHeld) : face_lanes(kHeld),
                                          book_keys ? book_dpad_lanes(kHeld) : dpad_lanes(kHeld), shoulder_lanes(kHeld));
        if ((kHeld & KEY_TOUCH) && (state == APP_PLAYING || state == APP_COUNTIN)) {   /* touch: the four highway quadrants are lanes too */
            const int tl = bb_view_touch_lane(touch_x, touch_y);
            if (tl >= 0) lanes |= (uint8_t)(1u << tl);
        }
        uint8_t lane_press = 0, lane_release = 0;
        bb_input_edges(prev_lanes, lanes, &lane_press, &lane_release);
        prev_lanes = lanes;
        if (state == APP_PLAYING) bb_view_lanes_pressed(lane_press);   /* receptor bounce: physical presses only */

        /* Stress smoke: mash lane 0 during play, retry after each game over, give up after N deaths */
        if (smoke_stress) {
            if (state == APP_PLAYING || state == APP_FAIL_TAPE) {
                if (frame_count % 4 == 0) lane_press |= 1;
                if (frame_count % 4 == 1) lane_release |= 1;
            }
            if (state == APP_GAMEOVER && state_ms > 700) {
                kDown |= (deaths >= 6) ? KEY_B : KEY_A;
            }
            if ((state == APP_MENU_LEVELS || state == APP_MENU_MAIN) && deaths >= 6) {
                goto quit;
            }
        }

        /* Scene music (main menu / shop / extras), streamed through the song path and restarted when it ends */
        if (!smoke || smoke_menus || smoke_uiperf || smoke_music || smoke_calib || smoke_preview || smoke_soundtest || smoke_sceneview) {
            const char* want = NULL;
            char want_buf[40];
            if (state == APP_MENU_LEVELS || state == APP_MENU_DIFF || state == APP_MENU_MODS || (state == APP_SCREEN && g_scr.node == SCN_POWERUP)) {
                /* level_select.gd stops the menu music; the highlighted level's preview plays instead (level_menu.gd:127-136) */
                int ps = -1;
                if (state == APP_MENU_LEVELS) {
                    int k = 0;
                    for (int i = 0; i < g_stage_count && ps < 0; i++) {
                        if (strcmp(g_stages[i].act, g_act_ids[cur_act]) != 0) continue;
                        if (k++ == cursor) ps = i;
                    }
                } else {
                    ps = stage;
                }
                if (ps >= 0) { snprintf(want_buf, sizeof(want_buf), "pv_%s", g_stages[ps].id); want = want_buf; }
                else want = "main";
            } else if (state == APP_MODMGR) {
                const int ps = mm_page == 1 && g_mod_count > 0 ? g_mod_rows[mm_lib] : (mm_page == 2 ? stage : -1);
                if (ps >= 0) { snprintf(want_buf, sizeof(want_buf), "pv_%s", g_stages[ps].id); want = want_buf; }
                else want = "main";
            } else if (state == APP_CALIB) want = "calib";
            else if (state == APP_MENU_SETTINGS && settings_from_pause) want = NULL;   /* the paused stage song stays loaded */
            else if (state <= APP_MENU_MODS) want = "main";
            else if (state == APP_SCREEN) {
                switch (g_scr.node) {
                    case SCN_SHOP: case SCN_SHOP_LIST: case SCN_SHOP_TALK: case SCN_SHOP_OUTFIT: want = "shop"; break;
                    case SCN_EXTRAS: case SCN_CHARS: case SCN_COLLECT: case SCN_GALLERY: case SCN_GALLERY_VIEW: case SCN_THEATER: want = "extras"; break;
                    default: want = "main"; break;
                }
            }
            {   /* Music / Scene Viewer: the track they ask for, by full path, played once (a finished stream is not restarted) */
                static char vcur[160] = "";
                const bool viewer = state == APP_SCREEN && (g_scr.node == SCN_MUSIC || g_scr.node == SCN_SCENEVIEW);
                if (viewer) {
                    const char* vp = g_scr.node == SCN_MUSIC ? bb_music_want() : bb_sv_want_music();
                    if (vp && strcmp(vp, vcur) != 0) {
                        bb_audio_stop_stream(&g_audio);
                        menu_music_ok = bb_audio_start_stream(&g_audio, vp, 120000) && g_audio.using_wav_file;
                        apply_volumes();
                        snprintf(vcur, sizeof(vcur), "%s", vp);
                        snprintf(g_track_buf, sizeof(g_track_buf), "viewer");
                        g_menu_track = g_track_buf;
                        g_track_preview = false;
                        bb_diag_log("viewer music %s ok=%d", vp, menu_music_ok);
                    } else if (!vp && (vcur[0] || (g_menu_track && strcmp(g_menu_track, "viewer") != 0))) {
                        bb_audio_stop_stream(&g_audio);
                        menu_music_ok = false;
                        vcur[0] = 0;
                        snprintf(g_track_buf, sizeof(g_track_buf), "viewer");
                        g_menu_track = g_track_buf;
                    }
                    if (vcur[0] && menu_music_ok) {
                        const int64_t pos = bb_audio_update(&g_audio);
                        if (bb_audio_stream_finished(&g_audio)) { bb_audio_stop_stream(&g_audio); menu_music_ok = false; }
                        if (g_scr.node == SCN_MUSIC) bb_music_set_pos(pos, bb_audio_duration_us(&g_audio));
                        else bb_sv_set_music_pos(pos);
                    }
                    want = NULL;
                } else {
                    vcur[0] = 0;
                }
            }
            if (want && strcmp(want, "main") == 0 && bb_theme_halloween()) want = "spooky";
            if (want && strcmp(want, "shop") == 0 && bb_theme_halloween()) want = "shop_h";   /* shop.gd HALLOWEEN: the slowed spooky theme, 80 BPM */
            if (want && !smoke_menus) {
                bool changed = !g_menu_track || strcmp(g_menu_track, want) != 0;
                if (changed || (menu_music_ok && bb_audio_stream_finished(&g_audio))) {
                    char mp[112];
                    snprintf(mp, sizeof(mp), "romfs:/music/%s.wav", want);
                    for (int i = 0; i < g_stage_count; i++)   /* a mod's level preview lives in its own folder (MOD2DS menu/preview.wav) */
                        if (g_stages[i].mod && strncmp(want, "pv_", 3) == 0 && strcmp(want + 3, g_stages[i].id) == 0) stage_path(mp, sizeof(mp), i, "menu/preview.wav");
                    bb_audio_stop_stream(&g_audio);
                    menu_music_ok = bb_audio_start_stream(&g_audio, mp, 120000) && g_audio.using_wav_file;
                    apply_volumes();
                    snprintf(g_track_buf, sizeof(g_track_buf), "%s", want);
                    g_menu_track = g_track_buf;
                    g_track_t0 = osGetTime();
                    g_track_preview = strncmp(want, "pv_", 3) == 0;
                    bb_diag_log("scene music %s ok=%d", want, menu_music_ok);
                }
                if (menu_music_ok) calib_pos_us = bb_audio_update(&g_audio);
                bb_ui_set_beat(!menu_music_ok ? -1.0f : (state == APP_CALIB ? (float)calib_pos_us / 1e6f * (100.0f / 60.0f) : (float)calib_pos_us / 1e6f * ((g_menu_track && strcmp(g_menu_track, "spooky") == 0 ? 150.0f : (g_menu_track && strcmp(g_menu_track, "shop_h") == 0 ? 80.0f : 128.0f)) / 60.0f)));
                if (menu_music_ok && g_track_preview && g_audio.ndsp_available) {   /* volume_db tween -40 -> 0 over 1.0 s */
                    float f = (float)(osGetTime() - g_track_t0) / 1000.0f;
                    if (f < 1.0f) {
                        float mix[12];
                        memset(mix, 0, sizeof(mix));
                        mix[0] = mix[1] = powf(10.0f, -40.0f * (1.0f - f) / 20.0f) * (float)g_save.settings.master_vol / 10.0f * (float)g_save.settings.music_vol / 10.0f;
                        ndspChnSetMix(BB_AUDIO_CHANNEL, mix);
                    } else if (f < 1.1f) {
                        apply_volumes();
                    }
                }
            } else if (!want && g_menu_track) {
                g_menu_track = NULL;
            }
        }

        /* ===================== state machine ===================== */
        const app_state_t pre_state = state;
        const int pre_cursor = cursor, pre_diff = diff, pre_mod = mod_cursor, pre_set = settings_cursor, pre_cat = opt_cat, pre_page = opt_page,
                  pre_pause = pause_cursor, pre_act = cur_act;
        switch (state) {
            case APP_TITLE: {   /* boot sequence: every screen fades in; A / a touch (or the timer) fades it out, then the next one */
                const uint64_t now = osGetTime();
                if (g_egg_on) {   /* the B easter egg plays out (no input), then the game exits */
                    const int64_t et = (int64_t)(now - g_egg_t0) * 1000;
                    bb_video_update(&g_egg, et);
                    if (bb_video_finished(&g_egg, et)) { bb_diag_log("warning: easter egg finished, exiting"); goto quit; }
                    break;
                }
                bb_boot_load();
                const int z = tap ? bb_ui_zone_hit(tap_x, tap_y) : -1;
                if (z == BB_ZONE_LEVEL) kDown |= KEY_A;
                if (z == BB_ZONE_BACK && boot_step == 0) kDown |= KEY_B;
                if (boot_step == 2 && g_intro_ok) bb_video_update(&g_intro, (int64_t)state_ms * 1000);
                if (boot_step == 2 && bb_theme_halloween()) bb_audio_update(&g_audio);   /* keep the splash sound streaming */
                if (boot_leave == 0) {
                    if (boot_step == 0 && (kDown & KEY_B)) {   /* "B to exit": the easter egg first (no clip in romfs: exit at once) */
                        if (!bb_video_init(&g_egg, "romfs:/boot/egg.bbvid", g_audio.ndsp_available, BB_VIDEO_CH + 1)) goto quit;
                        apply_volumes();
                        bb_video_reset(&g_egg);
                        g_egg_on = true;
                        g_egg_t0 = now;
                        aptSetHomeAllowed(false);   /* no HOME Menu during the clip (the game exits when it ends) */
                        bb_diag_log("warning: B -> easter egg");
                        break;
                    }
                    bool go = (kDown & (KEY_A | KEY_START)) != 0;
                    if (go && boot_step == 0) bb_uisfx_play(BB_UISFX_ACCEPT);
                    if (state_ms > BOOT_AUTO_MS && boot_step >= 1) go = true;
                    if (boot_step == 2 && (bb_theme_halloween() ? state_ms > BB_BOOT_HALLOWEEN_MS
                                                                : (g_intro_ok ? bb_video_finished(&g_intro, (int64_t)state_ms * 1000) : state_ms > 1500))) go = true;
                    if (go) boot_leave = now ? now : 1;
                } else if (now - boot_leave >= BB_BOOT_FADE_MS) {
                    boot_leave = 0;
                    if (boot_step >= 2) {
                        if (g_intro_ok) { bb_video_shutdown(&g_intro); g_intro_ok = false; }
                        if (bb_theme_halloween()) bb_audio_stop_stream(&g_audio);   /* the splash sound ends with its screen */
                        bb_boot_free();
                        ENTER(APP_MENU_MAIN);
                        cursor = 0;
                    } else {
                        boot_step++;
                        ENTER(APP_TITLE);
                        if (boot_step == 2) boot_intro_begin();   /* the intro starts with its screen */
                    }
                }
                break;
            }

            case APP_MENU_MAIN: {
                if ((tap && bb_ui_zone_hit(tap_x, tap_y) == BB_ZONE_THEME) || (kDown & KEY_X)) {
                    bb_ui_theme_pressed();
                    bb_uisfx_play(BB_UISFX_CLICK);
                    g_save.settings.theme = (uint8_t)(bb_theme_halloween() ? BB_THEME_DEFAULT : BB_THEME_HALLOWEEN);
                    bb_theme_set(g_save.settings.theme);
                    save_now("theme");
                    bb_audio_stop_stream(&g_audio);   /* SoundManager.stop_all_music */
                    menu_music_ok = false;
                    g_menu_track = NULL;
                    bb_ui_egg_reset();
                    bb_art_set_theme(bb_theme_halloween());
                    bb_diag_log("theme: %s", bb_theme_halloween() ? "halloween" : "default");
                    boot_step = 2;                    /* SceneManager.load_scene(splash): the intro (or the Halloween splash), then the menu */
                    boot_leave = 0;
                    ENTER(APP_TITLE);
                    boot_intro_begin();
                    break;
                }
                if (tap) {   /* touch: an entry is chosen and pressed at once; the BoobClick area counts easter-egg taps */
                    const int hit = bb_ui_main_menu_hit(tap_x, tap_y, cursor);
                    if (hit >= 0) {   /* first tap selects (focus sound), a tap on the selected entry presses it */
                        if (hit == cursor) kDown |= KEY_A;
                        else cursor = hit;
                    } else {
                        const int n = bb_ui_egg_tap(tap_x, tap_y);
                        if (n > 0) {   /* background_video._on_boob_click_button_down */
                            bb_uisfx_play_pitch(BB_UISFX_HIT_STAB, 1.0f + (float)(n - 1) * 0.05f);
                            if (n == 5) { bb_uisfx_play(BB_UISFX_SHUTTER); bb_uisfx_play(BB_UISFX_ARP); bb_diag_log("main menu: naked loop (5 taps)"); }
                        }
                    }
                }
                if (kRep & (KEY_DUP | KEY_CPAD_UP)) cursor = (cursor + MAIN_ITEMS - 1) % MAIN_ITEMS;
                if (kRep & (KEY_DDOWN | KEY_CPAD_DOWN)) cursor = (cursor + 1) % MAIN_ITEMS;
                if (kDown & KEY_B) {   /* B first moves to Quit (a stray B no longer closes the game); B on Quit quits */
                    if (cursor != MAIN_ITEMS - 1) cursor = MAIN_ITEMS - 1;
                    else goto quit;
                }
                if (kDown & KEY_A) {
                    main_cursor = cursor;
                    const bool new_scene = cursor == 0 || cursor == 2 || cursor == 3 || cursor == 4;   /* Play / Mods / Shop / Extras load scenes */
                    if (!smoke) {   /* MainMenuButton.play_ui_sound: scene buttons play ui_accept; Achievements is the drawer; Options and Quit are silent */
                        if (new_scene) bb_uisfx_play(BB_UISFX_ACCEPT);
                        else if (cursor == 5) bb_uisfx_play(BB_UISFX_DRAWER_OPEN);
                    }
                    if (new_scene) bb_ui_egg_reset();   /* the main menu comes back with the default loop */
                    switch (cursor) {
                        case 0: mod_flow = false; ENTER(APP_MENU_LEVELS); cursor = 0; break;
                        case 1: ENTER(APP_MENU_SETTINGS); settings_cursor = 0; opt_page = -1; opt_cat = 0; break;
                        case 2: mods_refresh(); mm_page = 0; mm_hub = 0; ENTER(APP_MODMGR); break;
                        case 3: bb_screens_open(&g_scr, SC_SHOP); ENTER(APP_SCREEN); screen_from_main = true; break;
                        case 4: bb_screens_open(&g_scr, SC_EXTRAS); ENTER(APP_SCREEN); screen_from_main = true; break;
                        case 5: bb_screens_open(&g_scr, SC_ACHIEVEMENTS); ENTER(APP_SCREEN); screen_from_main = true; break;
                        default: goto quit;
                    }
                }
                break;
            }

            case APP_SCREEN: {
                const bool was_ach = g_scr.node == SCN_ACH;
                if (g_scr.node == SCN_POWERUP) {   /* power-up row: drag / flick to scroll, tap selects, a tap on the selected icon plays */
                    static fling_t pfl;
                    int n = 1;
                    for (int it = BB_ITEM_POWERUP_EXTRA_LIVES; it <= BB_ITEM_POWERUP_HARDCORE; it++) if (bb_player_has_item(&g_save.player, it)) n++;
                    int c = g_scr.cursor[SCN_POWERUP], c0 = c;
                    float off;
                    const int z = fling_update(&pfl, tap, touch_up, touch_x, tap_x, tap_y, &c, n, 80.0f, &off);
                    if (z >= BB_ZONE_LEVEL && z < BB_ZONE_LEVEL + n) { if (z - BB_ZONE_LEVEL == c0) kDown |= KEY_A; else c = z - BB_ZONE_LEVEL; }
                    else if (z == BB_ZONE_BACK) kDown |= KEY_B;
                    if (c != c0) { g_scr.cursor[SCN_POWERUP] = c; bb_uisfx_play(BB_UISFX_TICK); }
                }
                if (g_scr.node == SCN_MUSIC) {   /* music_ui.c: the records on the bottom, the studio monitor on the top */
                    if (bb_music_update(kDown, kRep, tap, tap_x, tap_y) == 1) { bb_music_close(); g_scr.node = SCN_EXTRAS; }
                    if (bb_music_take_dirty() && !smoke) save_now("music");
                    break;
                }
                if (g_scr.node == SCN_SCENEVIEW) {   /* sceneview_ui.c: levels and loops on the bottom, the character on the top */
                    const char* sid = bb_sv_want_stage();
                    if (sid) {   /* main.c keeps owning the stage assets: load what the viewer asks for */
                        for (int i = 0; i < g_stage_count; i++) {
                            if (strcmp(g_stages[i].id, sid) != 0) continue;
                            load_stage_assets(i);
                            g_chart_ok = load_chart(i, 0);
                            bb_sv_stage_loaded(&g_chart, &g_anim, g_video, g_video_count);
                            break;
                        }
                    }
                    bb_sv_touch((kHeld & KEY_TOUCH) != 0, tap, touch_x, touch_y);   /* drag scrolls; buttons act on release */
                    if (bb_sv_update(kDown, kRep, false, 0, 0) == 1) { bb_sv_close(); g_scr.node = SCN_EXTRAS; }
                    break;
                }
                if (g_scr.node == SCN_GALLERY || g_scr.node == SCN_GALLERY_VIEW) {   /* gallery_ui.c: the grid on the bottom, the picture and its zoom on the top */
                    if (bb_gal_update(kDown, kRep, kHeld, tap, (kHeld & KEY_TOUCH) != 0, touch_x, touch_y) == 1) { bb_gal_close(); g_scr.node = SCN_EXTRAS; }
                    if (bb_gal_take_dirty() && !smoke_screens) save_now("gallery");
                    break;
                }
                if (g_scr.node == SCN_COLLECT) {   /* collection_ui.c: the book on the bottom, the popup on the top */
                    if (bb_coll_update(kDown, kRep, tap, tap_x, tap_y) == 1) { bb_coll_close(); g_scr.node = SCN_EXTRAS; }
                    if (bb_coll_take_dirty()) { save_now("collection: After Class unlocked"); bb_diag_log("progress: Dorm Key read -> After Class unlocked"); }
                    break;
                }
                if (g_scr.node != SCN_POWERUP) bb_screens_touch(&g_scr, tap, tap_x, tap_y, &kDown);
                int r = bb_screens_update(&g_scr, kDown, kRep);
                if (r == 1 && was_ach && !smoke) bb_uisfx_play(BB_UISFX_DRAWER_CLOSE);   /* achievements.gd play_toggle_sound */
                if (g_scr.dirty) { if (!smoke_screens) save_now("shop/extras"); g_scr.dirty = false; }
                if (r == 1) {
                    bb_screens_close(&g_scr);
                    if (screen_from_main) { ENTER(APP_MENU_MAIN); cursor = main_cursor; }
                    else { ENTER(APP_MENU_DIFF); }
                } else if (r == 2) {
                    g_powerup = g_scr.chosen_powerup;
                    if (!smoke) bb_uisfx_play(BB_UISFX_ACCEPT);                 /* power_up_selection_menu -> DifficultyNode.press */
                    bb_screens_close(&g_scr);
                    want_launch = true;
                } else if (r == 3) {   /* Theater: theater.gd fades the music and loads the cutscene; afterwards the Theater again */
                    const int rs = g_scr.theater_pick / 2;
                    theater_stage = -1;
                    for (int i = 0; i < g_stage_count; i++) if (rs < g_scr.reel_stages && strcmp(g_stages[i].id, g_scr.reel_id[rs]) == 0) theater_stage = i;
                    theater_post = g_scr.theater_pick & 1;
                    g_scr.result = 0;
                    if (theater_stage >= 0) {
                        launch_kind = 1;
                        want_launch = true;
                    }
                }
                break;
            }

            case APP_MODMGR: {
                const int z = tap ? bb_ui_zone_hit(tap_x, tap_y) : -1;
                if (z == BB_ZONE_BACK) kDown |= KEY_B;
                if (mm_page == 0) {   /* hub: Library | Modifiers, side by side */
                    if (z >= BB_ZONE_LEVEL && z < BB_ZONE_LEVEL + 2) { if (z - BB_ZONE_LEVEL == mm_hub) kDown |= KEY_A; else { mm_hub = z - BB_ZONE_LEVEL; bb_uisfx_play(BB_UISFX_TOUCH); } }
                    if ((kRep & (KEY_DLEFT | KEY_DRIGHT | KEY_CPAD_LEFT | KEY_CPAD_RIGHT)) != 0) { mm_hub ^= 1; bb_uisfx_play(BB_UISFX_TOUCH); }
                    if (kDown & KEY_B) { bb_uisfx_play(BB_UISFX_BACK); mods_release(); ENTER(APP_MENU_MAIN); cursor = main_cursor; break; }
                    if (kDown & KEY_A) {
                        bb_uisfx_play(BB_UISFX_ACCEPT);
                        if (mm_hub == 0) { mm_page = 1; ENTER(APP_MODMGR); }
                        else { mods_return = 1; ENTER(APP_MENU_MODS); }
                    }
                } else if (mm_page == 1) {   /* library list */
                    const int c0 = mm_lib;
                    if (z >= BB_ZONE_LEVEL && z < BB_ZONE_LEVEL + g_mod_count) { if (z - BB_ZONE_LEVEL == mm_lib) kDown |= KEY_A; else mm_lib = z - BB_ZONE_LEVEL; }
                    if (g_mod_count > 0) {
                        if (kRep & (KEY_DLEFT | KEY_CPAD_LEFT)) mm_lib = mm_lib > 0 ? mm_lib - 1 : 0;   /* left / right scroll too */
                        if (kRep & (KEY_DRIGHT | KEY_CPAD_RIGHT)) mm_lib = mm_lib + 1 < g_mod_count ? mm_lib + 1 : g_mod_count - 1;
                        if (kRep & (KEY_DUP | KEY_CPAD_UP)) mm_lib = (mm_lib + g_mod_count - 1) % g_mod_count;
                        if (kRep & (KEY_DDOWN | KEY_CPAD_DOWN)) mm_lib = (mm_lib + 1) % g_mod_count;
                        if (kRep & KEY_L) mm_lib = mm_lib >= 4 ? mm_lib - 4 : 0;
                        if (kRep & KEY_R) mm_lib = mm_lib + 4 < g_mod_count ? mm_lib + 4 : g_mod_count - 1;
                    }
                    if (mm_lib != c0) bb_uisfx_play(BB_UISFX_TICK);
                    if (kDown & KEY_B) { bb_uisfx_play(BB_UISFX_BACK); mm_page = 0; ENTER(APP_MODMGR); break; }
                    if ((kDown & KEY_A) && g_mod_count > 0) {
                        bb_uisfx_play(BB_UISFX_CLICK_ACCEPT);
                        stage = g_mod_rows[mm_lib];
                        mm_page = 2; mm_armed = false;
                        ENTER(APP_MODMGR);
                    }
                } else {   /* a mod's screen: the portrait (or A) opens its difficulty select, Y / the band opens the modifiers */
                    if (z == BB_ZONE_LEVEL) { if (mm_armed) kDown |= KEY_A; else { mm_armed = true; bb_uisfx_play(BB_UISFX_TOUCH); } }
                    if (z == BB_ZONE_MODS) kDown |= KEY_Y;
                    if (kDown & KEY_B) { bb_uisfx_play(BB_UISFX_BACK); mm_page = 1; ENTER(APP_MODMGR); break; }
                    if (kDown & KEY_Y) { bb_uisfx_play(BB_UISFX_ACCEPT); mods_return = 2; ENTER(APP_MENU_MODS); break; }
                    if (kDown & KEY_A) {
                        bb_uisfx_play(BB_UISFX_CLICK_ACCEPT);
                        mod_flow = true;
                        mods_return = 0;
                        g_chart_ok = load_chart(stage, diff);
                        bb_core_init(&g_core, &g_chart);
                        opt_autoplay = smoke_auto || smoke_perf;
                        ENTER(APP_MENU_DIFF);
                    }
                }
                break;
            }

            case APP_MENU_LEVELS: {
                int rows[MAX_STAGES], nrows = 0;
                for (int i = 0; i < g_stage_count; i++) if (strcmp(g_stages[i].act, g_act_ids[cur_act]) == 0) rows[nrows++] = i;
                if (nrows == 0) { cur_act = 0; break; }
                if (cursor >= nrows) cursor = nrows - 1;
                {   /* touch: drag sideways to scroll the portraits; a tap selects a level, a tap on the selected one opens it
                     * (LevelNode._on_clickabe_area_component_button_up); volume nodes switch volumes */
                    static fling_t fl;
                    float off = 0.0f;
                    const int zone = fling_update(&fl, tap, touch_up, touch_x, tap_x, tap_y, &cursor, nrows, 80.0f, &off);
                    bb_ui_level_drag(off);
                    {
                        const int zone0 = zone;
                        if (zone0 >= 0) {
                            if (zone0 >= BB_ZONE_ACT) { if (zone0 - BB_ZONE_ACT != cur_act) { cur_act = zone0 - BB_ZONE_ACT; cursor = 0; break; } }
                            else if (zone0 >= BB_ZONE_LEVEL) { if (zone0 - BB_ZONE_LEVEL == cursor) kDown |= KEY_A; else cursor = zone0 - BB_ZONE_LEVEL; }
                            else if (zone0 == BB_ZONE_PREV && cursor > 0) cursor--;
                            else if (zone0 == BB_ZONE_NEXT && cursor + 1 < nrows) cursor++;
                            else if (zone0 == BB_ZONE_BACK) kDown |= KEY_B;
                        }
                    }
                }
                /* the circle pad and D-pad left / right scroll the levels (never the volume); L / R switch volumes */
                if ((kRep & (KEY_CPAD_LEFT | KEY_DLEFT)) && cursor > 0) cursor--;
                if ((kRep & (KEY_CPAD_RIGHT | KEY_DRIGHT)) && cursor + 1 < nrows) cursor++;
                if (kDown & KEY_L) { cur_act = (cur_act + g_act_count - 1) % g_act_count; cursor = 0; }
                if (kDown & KEY_R) { cur_act = (cur_act + 1) % g_act_count; cursor = 0; }
                if (kDown & KEY_B) { bb_ui_level_card_free(); ENTER(APP_MENU_MAIN); cursor = 0; break; }
                if (kDown & KEY_A) {
                    int si = rows[cursor];
                    if (!stage_open(si)) {
                        if (!smoke) bb_uisfx_play(BB_UISFX_LOCK);               /* level_menu: locked or coming soon */
                    } else {
                        if (!smoke) bb_uisfx_play(BB_UISFX_CLICK_ACCEPT);       /* level_menu._on_level_selected */
                        stage = si;
                        g_chart_ok = load_chart(stage, diff);
                        bb_core_init(&g_core, &g_chart);
                        opt_autoplay = smoke_auto || smoke_perf;
                        ENTER(APP_MENU_DIFF);
                    }
                }
                break;
            }

            case APP_MENU_DIFF: {
                const bb_stage_save_t* dsv = bb_save_find_stage(&g_save, g_stages[stage].id);
                if (tap) {
                    const int z = bb_ui_zone_hit(tap_x, tap_y);
                    if (z >= BB_ZONE_LEVEL && z < BB_ZONE_LEVEL + 3) { if (z - BB_ZONE_LEVEL == diff) kDown |= KEY_A; else diff = z - BB_ZONE_LEVEL; }
                    else if (z == BB_ZONE_HANDSFREE) kDown |= KEY_Y;
                    else if (z == BB_ZONE_MODS) kDown |= KEY_R;
                    else if (z == BB_ZONE_BACK) kDown |= KEY_B;
                }
                if (kDown & (KEY_DUP | KEY_DLEFT)) diff = (diff + 2) % 3;
                if (kDown & (KEY_DDOWN | KEY_DRIGHT)) diff = (diff + 1) % 3;
                if (kDown & KEY_B) { ENTER_LEVELS(); break; }
                if (kDown & KEY_R) { ENTER(APP_MENU_MODS); mod_cursor = 0; break; }
                if (smoke && !smoke_menus && !smoke_screens && state_ms > 500) {
                    g_powerup = 0;
                    want_launch = true;
                } else if ((kDown & KEY_Y) && ((dsv && dsv->best_grade[diff] >= 4) || g_stages[stage].mod)) {
                    opt_autoplay = true;           /* Play Hands Free: unlocked by 4+ stars on this difficulty */
                    bb_uisfx_play(BB_UISFX_ACCEPT);
                    g_powerup = 0;
                    want_launch = true;
                } else if (kDown & KEY_A) {
                    opt_autoplay = smoke_auto || smoke_perf;
                    if (bb_has_powerups(&g_save)) {   /* PowerUpSelectionMenu appears when any power-up is owned */
                        bb_screens_open(&g_scr, SC_POWERUP);
                        ENTER(APP_SCREEN);
                        screen_from_main = false;
                    } else {
                        bb_uisfx_play(BB_UISFX_ACCEPT);                         /* DifficultyNode.press */
                        g_powerup = 0;
                        want_launch = true;
                    }
                }
                break;
            }

            case APP_MENU_MODS: {
                const int mod_rows = stage_is_legacy(stage) ? 4 : 3;
                if (tap) {   /* touch: a row is selected first, a tap on the selected row changes it */
                    const int z = bb_ui_zone_hit(tap_x, tap_y);
                    if (z >= BB_ZONE_LEVEL && z < BB_ZONE_LEVEL + mod_rows) { if (z - BB_ZONE_LEVEL == mod_cursor) kDown |= KEY_A; else mod_cursor = z - BB_ZONE_LEVEL; }
                    else if (z == BB_ZONE_BACK) kDown |= KEY_B;
                }
                if (mod_cursor >= mod_rows) mod_cursor = 0;
                if (kDown & KEY_DUP) mod_cursor = (mod_cursor + mod_rows - 1) % mod_rows;
                if (kDown & KEY_DDOWN) mod_cursor = (mod_cursor + 1) % mod_rows;
                if (kDown & (KEY_B | KEY_L)) {
                    save_now("modifiers");
                    if (mods_return == 1) { mm_page = 0; ENTER(APP_MODMGR); }
                    else if (mods_return == 2) { mm_page = 2; mm_armed = false; ENTER(APP_MODMGR); }
                    else ENTER(APP_MENU_DIFF);
                    break;
                }
                if (kDown & (KEY_A | KEY_DRIGHT | KEY_DLEFT)) {
                    if (mod_cursor == 0) g_save.settings.scroll_idx = (uint8_t)((g_save.settings.scroll_idx + 1) % BB_SCROLL_STEPS);   /* Note Speed switcher */
                    else if (mod_cursor == 1) mod_no_hud = !mod_no_hud;
                    else if (mod_cursor == 2) mod_no_beat_bar = !mod_no_beat_bar;
                    else mod_legacy_layout = !mod_legacy_layout;
                    bb_view_set_modifiers(mod_no_hud, mod_no_beat_bar);
                    if (!smoke) bb_uisfx_play(BB_UISFX_CLICK);                  /* modifier_node._on_button_up */
                }
                break;
            }

            case APP_MENU_SETTINGS: {
                bb_settings_t* st = &g_save.settings;
                if (tap || ((kHeld & KEY_TOUCH) && opt_page == 0)) {   /* touch: entries select then press; slider bars follow the stylus */
                    const int z = bb_ui_zone_hit(tap_x, tap_y);
                    float zx, zw;
                    if (z >= BB_ZONE_ACT && z < BB_ZONE_ACT + 4 && opt_page == 0 && bb_ui_zone_rect(z, &zx, &zw)) {
                        int v = (int)floorf(((float)tap_x - zx - 6.0f) / (zw - 12.0f) * 10.0f + 0.5f);
                        v = v < 0 ? 0 : (v > 10 ? 10 : v);
                        uint8_t* vol[4] = { &st->master_vol, &st->music_vol, &st->sfx_vol, &st->voice_vol };
                        settings_cursor = z - BB_ZONE_ACT;
                        if (*vol[settings_cursor] != (uint8_t)v) { *vol[settings_cursor] = (uint8_t)v; apply_volumes(); bb_uisfx_play(BB_UISFX_TOUCH); }
                    } else if (tap && z == BB_ZONE_BACK) {
                        kDown |= KEY_B;
                    } else if (tap && z >= BB_ZONE_LEVEL && z < BB_ZONE_LEVEL + 7) {
                        const int i = z - BB_ZONE_LEVEL;
                        if (opt_page < 0) { if (i == opt_cat) kDown |= KEY_A; else opt_cat = i; }
                        else { if (i == settings_cursor) kDown |= KEY_A; else settings_cursor = i; }
                    }
                }
                if (opt_page < 0) {
                    /* category dialog: Audio | Gameplay / Controls | Data, then Main Menu and Quit Game */
                    /* Audio | Gameplay / Controls | Data / Credits, then Main Menu and Quit Game */
                    int c = opt_cat;
                    if (c < 4) {
                        if (kDown & (KEY_DLEFT | KEY_DRIGHT)) c ^= 1;
                        if (kDown & KEY_DDOWN) c = c < 2 ? c + 2 : 4;
                        if (kDown & KEY_DUP) c = c < 2 ? 6 : c - 2;
                    } else {
                        if (kDown & KEY_DDOWN) c = c == 6 ? 0 : c + 1;
                        if (kDown & KEY_DUP) c = c == 4 ? 2 : c - 1;
                    }
                    opt_cat = c;
                    if (kDown & KEY_A) {
                        if (opt_cat <= 4) { opt_page = opt_cat; settings_cursor = 0; bb_ui_enter(); }
                        else if (opt_cat == 5) {   /* hot_menu exit to main menu: ui_back (from a paused level the level ends) */
                            bb_uisfx_play(BB_UISFX_BACK); save_now("settings");
                            if (settings_from_pause) { settings_from_pause = false; bb_audio_stop_stream(&g_audio); bb_sfx_stop(&g_sfx); videos_set_paused(false); ENTER(APP_MENU_MAIN); cursor = 0; }
                            else { ENTER(APP_MENU_MAIN); cursor = 4; }
                        }
                        else goto quit;
                    }
                    if (kDown & KEY_B) {
                        save_now("settings");
                        if (settings_from_pause) { settings_from_pause = false; ENTER(APP_PAUSED); pause_cursor = 2; }
                        else { ENTER(APP_MENU_MAIN); cursor = 4; }
                    }
                    break;
                }
                if (opt_page == 4) {   /* Credits: D-Pad scrolls (drawn by bb_ui_credits), B back to the categories */
                    if (kDown & KEY_B) { bb_uisfx_play(BB_UISFX_BACK); opt_page = -1; bb_ui_enter(); }
                    break;
                }
                static const int rows_in_tab[4] = { 4, BB_DEV_BUILD ? 5 : 4, 1, 3 };
                if (kDown & KEY_R) { opt_page = (opt_page + 1) % 4; settings_cursor = 0; }
                if (kDown & KEY_L) { opt_page = (opt_page + 3) % 4; settings_cursor = 0; }
                int nrows = rows_in_tab[opt_page];
                if (kRep & KEY_DUP) settings_cursor = (settings_cursor + nrows - 1) % nrows;
                if (kRep & KEY_DDOWN) settings_cursor = (settings_cursor + 1) % nrows;
                int dir = ((kRep & KEY_DRIGHT) ? 1 : 0) - ((kRep & KEY_DLEFT) ? 1 : 0);
                int id = opt_page * 10 + settings_cursor;
                if (!BB_DEV_BUILD && opt_page == 1 && settings_cursor >= 1) id++;   /* the hidden unlock-all row (11) keeps the other ids */
                if (dir != 0 && !smoke) bb_uisfx_play(opt_page == 0 ? BB_UISFX_TOUCH : BB_UISFX_ANALOG_CLICK);   /* sliders: settings_slider.gd:33 touch; switches/values: sfx_analog_click */
                if (dir != 0) {
                    switch (id) {
                        case 0: { int c = st->master_vol + dir; if (c < 0) c = 0; if (c > 10) c = 10; st->master_vol = (uint8_t)c; apply_volumes(); break; }
                        case 1: { int c = st->music_vol + dir; if (c < 0) c = 0; if (c > 10) c = 10; st->music_vol = (uint8_t)c; apply_volumes(); break; }
                        case 2: { int c = st->sfx_vol + dir; if (c < 0) c = 0; if (c > 10) c = 10; st->sfx_vol = (uint8_t)c; apply_volumes(); break; }
                        case 3: { int c = st->voice_vol + dir; if (c < 0) c = 0; if (c > 10) c = 10; st->voice_vol = (uint8_t)c; apply_volumes(); break; }
                        case 13: st->reduce_motion = (uint8_t)!st->reduce_motion; bb_ui_set_reduce_motion(st->reduce_motion != 0); break;
                        case 14: st->scroll_mode = (uint8_t)((st->scroll_mode + BB_SCROLL_MODE_COUNT + dir) % BB_SCROLL_MODE_COUNT); break;
                        case 10: { int c = st->calibration_ms + dir * 5; if (c < -BB_CAL_LIMIT_MS) c = -BB_CAL_LIMIT_MS; if (c > BB_CAL_LIMIT_MS) c = BB_CAL_LIMIT_MS; st->calibration_ms = (int16_t)c; break; }
                        case 11: st->unlock_all = (uint8_t)!st->unlock_all; break;
                        case 12: st->skip_cutscene = (uint8_t)!st->skip_cutscene; break;
                        case 20: st->control_mode = (uint8_t)bb_input_mode_step((bb_control_mode_t)st->control_mode, dir, bb_hw()->has_zlzr); break;
                        default: break;
                    }
                }
                if (kDown & KEY_A) {
                    if (id == 10) {
                        if (settings_from_pause) { bb_uisfx_play(BB_UISFX_LOCK); break; }   /* the calibrator needs its own music: not from a paused level */
                        ENTER(APP_CALIB); break;
                    }
                    if (id == 13) { st->reduce_motion = (uint8_t)!st->reduce_motion; bb_ui_set_reduce_motion(st->reduce_motion != 0); bb_uisfx_play(BB_UISFX_ANALOG_CLICK); }
                    if (id == 11) st->unlock_all = (uint8_t)!st->unlock_all;
                    if (id == 12) st->skip_cutscene = (uint8_t)!st->skip_cutscene;
                    if (id == 14) { st->scroll_mode = (uint8_t)((st->scroll_mode + 1) % BB_SCROLL_MODE_COUNT); bb_uisfx_play(BB_UISFX_ANALOG_CLICK); }
                    if (id == 20) st->control_mode = (uint8_t)bb_input_mode_step((bb_control_mode_t)st->control_mode, 1, bb_hw()->has_zlzr);
                    if (id == 30) {   /* Restore Settings To Default (settings only; scores, progress and purchases stay) */
                        bb_save_t fresh;
                        bb_save_defaults(&fresh);
                        g_save.settings = fresh.settings;
                        apply_volumes();
                        bb_ui_set_reduce_motion(false);
                        bb_uisfx_play(BB_UISFX_ANALOG_CLICK);
                        save_now("restore settings");
                    }
                    if (id == 31) { ENTER(APP_MENU_CONFIRM_RESET); confirm_cursor = 0; break; }
                    if (id == 32) { bb_uisfx_play(BB_UISFX_BACK); opt_page = -1; bb_ui_enter(); }   /* back_button pressed sound */
                }
                if (kDown & KEY_B) { opt_page = -1; bb_ui_enter(); }
                break;
            }

            case APP_CALIB: {   /* calibrator.tscn: L/R change the offset (5 ms steps here), B returns with ui_back */
                bb_settings_t* st = &g_save.settings;
                int dir = ((kRep & KEY_DRIGHT) ? 1 : 0) - ((kRep & KEY_DLEFT) ? 1 : 0);
                if (dir != 0) {
                    int c = st->calibration_ms + dir * 5;
                    if (c < -BB_CAL_LIMIT_MS) c = -BB_CAL_LIMIT_MS;
                    if (c > BB_CAL_LIMIT_MS) c = BB_CAL_LIMIT_MS;
                    st->calibration_ms = (int16_t)c;
                }
                if (kDown & KEY_B) {
                    bb_uisfx_play(BB_UISFX_BACK);
                    save_now("calibration");
                    ENTER(APP_MENU_SETTINGS);
                    opt_page = 1;
                    settings_cursor = 0;
                }
                break;
            }

            case APP_MENU_CONFIRM_RESET: {
                if (kDown & (KEY_DUP | KEY_DDOWN)) confirm_cursor = !confirm_cursor;
                if (kDown & KEY_B) { ENTER(APP_MENU_SETTINGS); break; }
                if (kDown & KEY_A) {
                    if (confirm_cursor == 1) {
                        bb_save_defaults(&g_save);
                        bb_ui_set_reduce_motion(false);
                        save_now("reset");
                        apply_volumes();
                    }
                    ENTER(APP_MENU_SETTINGS);
                }
                break;
            }

            case APP_CUTSCENE: {
                if (tap) {   /* bottom-screen buttons: Skip, Autoplay, Next */
                    const int z = bb_ui_zone_hit(tap_x, tap_y);
                    if (z == BB_ZONE_LEVEL) kDown |= KEY_B;
                    else if (z == BB_ZONE_LEVEL + 1) kDown |= KEY_Y;
                    else if (z == BB_ZONE_LEVEL + 2) kDown |= KEY_A;
                }
                if (kDown & KEY_B) bb_ui_cutscene_tapped(0);   /* the strip lights the button that acted (touch or the real button) */
                if (kDown & KEY_Y) bb_ui_cutscene_tapped(1);
                if (kDown & (KEY_A | KEY_R | KEY_DRIGHT)) bb_ui_cutscene_tapped(2);
                if (kDown & KEY_Y) { bb_cs_set_autoplay(!bb_cs_autoplay()); bb_uisfx_play(BB_UISFX_CLICK); }
                if (smoke_cutscene && !bb_cs_autoplay() && (int)(state_ms / 900) > smoke_cs_adv) { kDown |= KEY_A; smoke_cs_adv = (int)(state_ms / 900); }
                if (bb_cs_update(kDown, (float)dt_ms / 1000.0f)) {
                    smoke_cs_adv = 0;
                    smoke_cs_cap = -1;
                    if (cs_after == 0) {
                        launch_skip_cs = true;
                        want_launch = true;
                    } else if (cs_after == 2) {
                        if (open_cutscene(stage, "post")) { cs_after = 3; ENTER(APP_CUTSCENE); }
                        else { bb_diag_log("SMOKE END cutscene (no post)"); goto quit; }
                    } else if (cs_after == 3) {
                        bb_diag_log("SMOKE END cutscene stage=%s", g_stages[stage].id);
                        goto quit;
                    } else if (cs_after == 4) {   /* back to the Theater, on the reel just watched */
                        bb_screens_open(&g_scr, SC_EXTRAS);
                        g_scr.cursor[SCN_EXTRAS] = 2;
                        g_scr.node = SCN_THEATER;
                        screen_from_main = true;
                        ENTER(APP_SCREEN);
                    } else {
                        ENTER_LEVELS();
                    }
                }
                break;
            }

            case APP_PLAYING:
            case APP_FAIL_TAPE:
            case APP_OUTRO: {
                if (state == APP_PLAYING && (kDown & KEY_START)) {
                    bb_uisfx_play(BB_UISFX_CLICK);                              /* game_pause_menu.toggle_pause */
                    bb_core_pause(&g_core);
                    bb_audio_set_paused(&g_audio, true);
                    videos_set_paused(true);
                    ENTER(APP_PAUSED);
                    pause_cursor = 0;
                    break;
                }

                /* Smoke mode: inject realistic presses (+8/+48/-85 ms) as lane presses through the same core calls */
                if (smoke && !smoke_fail && !smoke_stress && state == APP_PLAYING && !g_core.autoplay) {
                    while (smoke_cursor < g_core.chart.note_count) {
                        const bb_note_t* t = &g_core.chart.notes[smoke_cursor];
                        int64_t err = 8000;
                        if (smoke_cursor % 5 == 2) err = 48000;
                        else if (smoke_cursor % 7 == 4) err = -85000;
                        int64_t est = audio_pos_us + g_core.calibration_offset_us - g_core.chart.song_offset_us;
                        if (est < t->timestamp_us + err) break;
                        lane_press |= (uint8_t)(1u << (t->input_type & 3));
                        smoke_cursor++;
                        break;
                    }
                }

                if (state == APP_PLAYING && !g_core.autoplay) {
                    for (uint8_t l = 0; l < 4; l++) {
                        if (lane_press & (1u << l)) bb_core_press(&g_core, l, audio_pos_us);
                    }
                    for (uint8_t l = 0; l < 4; l++) {
                        if (lane_release & (1u << l)) bb_core_release(&g_core, l, audio_pos_us);
                    }
                }
                if (state == APP_PLAYING) {
                    bb_core_step(&g_core, audio_pos_us);
                    bb_sfx_tick(&g_sfx, g_core.song_cur_time_us);
                }
                videos_update(g_core.song_cur_time_us);

                bb_event_t ev;
                while (bb_core_poll_event(&g_core, &ev)) {
                    bb_view_event(&g_view, &g_core, &ev);
                    sfx_for_event(&ev);
                }

                if (state == APP_PLAYING && g_core.state == BB_STATE_FAILED) {
                    ENTER(APP_FAIL_TAPE);
                    deaths++;
                    caption = (caption + 1) % 3;
                    play_shared_sfx((caption == 0) ? "__glass1" : (caption == 1) ? "__glass2" : "__glass3");
                    bb_uisfx_play(bb_theme_halloween() ? BB_UISFX_FAIL_HALLOWEEN : BB_UISFX_WHITE_NOISE);   /* StudioWhiteNoiseSound (AudioStreamSeasonal: halloween stream), until Retry / Give Up */
                    fail_focus = 0;
                    bb_diag_log("FAILED #%lu t=%+.2fs score=%ld", (unsigned long)deaths, (double)g_core.song_cur_time_us / 1e6, (long)g_core.stats.score);
                } else if (state == APP_PLAYING && g_core.state == BB_STATE_COMPLETED) {
                    outro_base_us = g_core.song_cur_time_us;
                    ENTER(APP_OUTRO);
                    bb_diag_log("COMPLETED t=%+.2fs", (double)g_core.song_cur_time_us / 1e6);
                }

                if (state == APP_FAIL_TAPE) {
                    /* game_music._on_level_failed: pitch tweens to ~0 over 2 s */
                    float t = (float)state_ms / 2000.0f;
                    if (t > 1.0f) t = 1.0f;
                    bb_audio_set_pitch(&g_audio, 1.0f - 0.999f * t);
                    if (state_ms >= 2000) {
                        bb_audio_stop_stream(&g_audio);
                        bb_sfx_stop(&g_sfx);
                        videos_set_paused(false);
                        if (!g_core.autoplay) {
                            bb_run_t run;
                            memset(&run, 0, sizeof(run));
                            run.stage_id = g_stages[stage].id;
                            run.level_enum = g_stages[stage].level_enum;
                            run.diff = diff;
                            run.score = g_core.stats.score;
                            run.powerup = g_powerup;
                            run.clean = !smoke || smoke_progress;   /* the progress smoke is a real, rewarded run */
                            run.mod = g_stages[stage].mod;
                            run.fail_time_us = g_core.song_cur_time_us;
                            memset(&outcome, 0, sizeof(outcome));
                            bb_run_failed(&g_save, &run, &outcome);
                            save_now("failed");
                        }
                        ENTER(APP_GAMEOVER);
                        bb_diag_log("GAMEOVER shown");
                    }
                }
                if (state == APP_OUTRO) {
                    /* The reference passes the level once the climax video has finished and the music has stopped.
                     * The core stops at COMPLETED and the audio clock stops at the song end, but videos can outlast
                     * the song: drive the chart clock from wall time for the rest of the outro. */
                    g_core.song_cur_time_us = outro_base_us + (int64_t)state_ms * 1000;
                    bool song_done = bb_audio_stream_finished(&g_audio) || bb_audio_duration_us(&g_audio) == 0;
                    if ((song_done && videos_all_finished(g_core.song_cur_time_us)) || state_ms > 130000) {   /* a final video can run ~100 s */
                        bb_audio_stop_stream(&g_audio);
                        memset(&outcome, 0, sizeof(outcome));
                        bool new_best = false, paid = false;
                        int32_t pb_before = 0;
                        {
                            const bb_stage_save_t* psv0 = bb_save_find_stage(&g_save, g_stages[stage].id);
                            pb_before = psv0 ? psv0->best_score[diff] : 0;
                        }
                        if (!g_core.autoplay && (!g_core.no_fail || g_powerup == BB_ITEM_POWERUP_NO_FAIL) && g_core.stats.grade != BB_GRADE_NONE && (!g_stages[stage].mod || bb_save_find_stage(&g_save, g_stages[stage].id) || mod_record_room())) {
                            new_best = bb_save_record_result(&g_save, g_stages[stage].id, diff, g_core.stats.score,
                                                             (uint8_t)g_core.stats.grade, (uint16_t)g_core.stats.max_combo);
                            bb_save_complete_level(&g_save, g_stages[stage].act, (uint8_t)g_stages[stage].level_index);
                        }
                        if (!g_core.autoplay && g_core.stats.grade != BB_GRADE_NONE) {
                            bb_run_t run;
                            memset(&run, 0, sizeof(run));
                            run.stage_id = g_stages[stage].id;
                            run.level_enum = g_stages[stage].level_enum;
                            run.diff = diff;
                            run.grade = (int)g_core.stats.grade;
                            run.hit = g_core.stats.hit;
                            run.registered = g_core.stats.registered;
                            run.score = g_core.stats.score;
                            run.max_combo = g_core.stats.max_combo;
                            run.powerup = g_powerup;
                            run.clean = !smoke || smoke_progress;   /* the progress smoke is a real, rewarded run */
                            run.mod = g_stages[stage].mod;
                            bb_run_passed(&g_save, &run, &outcome);
                            paid = run.clean;
                        }
                        if (g_video_count > 0) {   /* the climax stays on its last frame behind the results */
                            const bb_video_t* lv = &g_video[g_video_count - 1];
                            int64_t end = lv->start_us + (int64_t)((double)lv->frames * 1000000.0 / (double)lv->fps) - 20000;
                            if (end > lv->hide_us - 1000) end = lv->hide_us - 1000;
                            if (end >= lv->start_us) g_core.song_cur_time_us = end;
                        }
                        fill_results(stage, diff, pb_before, new_best, paid, &outcome);
                        results_focus = 0;
                        bb_results_begin();
                        save_now("results");
                        ENTER(APP_RESULTS);
                        bb_diag_log("RESULTS shown");
                    }
                }
                break;
            }

            case APP_PAUSED: {   /* game_pause_menu.gd: Resume, Restart, Settings, Level Select, Exit To Menu (bottom screen, touch) */
                if (tap) {   /* first tap focuses, a tap on the focused button presses it */
                    const int z = bb_ui_zone_hit(tap_x, tap_y);
                    if (z >= BB_ZONE_PAUSE && z < BB_ZONE_PAUSE + BB_PAUSE_ITEMS) { if (z - BB_ZONE_PAUSE == pause_cursor) kDown |= KEY_A; else pause_cursor = z - BB_ZONE_PAUSE; }
                }
                if (kDown & KEY_DUP) pause_cursor = (pause_cursor + BB_PAUSE_ITEMS - 1) % BB_PAUSE_ITEMS;
                if (kDown & KEY_DDOWN) pause_cursor = (pause_cursor + 1) % BB_PAUSE_ITEMS;
                if ((kDown & (KEY_START | KEY_B)) || ((kDown & KEY_A) && pause_cursor == 0)) {   /* toggle_pause: ui_click, then the countdown */
                    bb_uisfx_play(BB_UISFX_CLICK);
                    ENTER(APP_COUNTIN);
                } else if ((kDown & KEY_A) && pause_cursor == 1) {   /* _on_restart_button_up */
                    bb_uisfx_play(BB_UISFX_CLICK);
                    start_stage(stage, diff, opt_no_fail, opt_autoplay);
                    prev_lanes = lanes;
                    smoke_cursor = 0;
                    ENTER(APP_PLAYING);
                } else if ((kDown & KEY_A) && pause_cursor == 2) {   /* _on_settings_button_up: the settings dialog over the paused level */
                    bb_uisfx_play(BB_UISFX_CLICK);
                    settings_from_pause = true;
                    ENTER(APP_MENU_SETTINGS);
                    settings_cursor = 0; opt_page = -1; opt_cat = 0;
                } else if ((kDown & KEY_A) && pause_cursor >= 3) {   /* _on_level_select_button_up (ui_back) / _on_exit_button_up (ui_click) */
                    bb_uisfx_play(pause_cursor == 3 ? BB_UISFX_BACK : BB_UISFX_CLICK);
                    bb_audio_stop_stream(&g_audio);
                    bb_sfx_stop(&g_sfx);
                    videos_set_paused(false);
                    if (pause_cursor == 3) ENTER_LEVELS();
                    else { ENTER(APP_MENU_MAIN); cursor = 0; }
                }
                break;
            }

            case APP_COUNTIN: {   /* PauseMenu "countdown": 3 s, then the song continues where it stopped */
                if (kDown & KEY_START) {   /* changed their mind: back to the pause menu */
                    bb_uisfx_play(BB_UISFX_CLICK);
                    ENTER(APP_PAUSED);
                    pause_cursor = 0;
                } else if (state_ms >= 3000) {
                    bb_audio_set_paused(&g_audio, false);
                    videos_set_paused(false);
                    bb_core_resume(&g_core);
                    prev_lanes = lanes;   /* lanes already held when the music returns are not presses */
                    ENTER(APP_PLAYING);
                }
                break;
            }

            case APP_LOADING: {   /* loading.tscn: a short animated screen, then the blocking stage load under its last frame */
                bb_ui_set_beat((float)state_ms / 1000.0f * (128.0f / 60.0f));   /* the logo keeps the menu's 128 BPM pulse */
                if (state_ms >= LOAD_OUT_MS + LOAD_FADE_MS + 40) {   /* a fully black frame is on screen: load under it */
                    load_stage_assets(stage);
                    start_stage(stage, diff, opt_no_fail, opt_autoplay);
                    bb_ui_level_card_free();
                    prev_lanes = lanes;
                    smoke_cursor = 0;
                    ENTER(APP_PLAYING);
                    play_fade_t0 = osGetTime();   /* the stage fades in from black (presentation only; the song starts at once) */
                }
                break;
            }

            case APP_GAMEOVER:
            case APP_RESULTS: {
                if (state == APP_RESULTS) {
                    bb_results_update(&g_res, (int64_t)state_ms);
                    /* the first press (or tap) while the sequence is still playing completes it; the next one acts */
                    if (state_ms < BB_RESULTS_ANIM_MS && ((kDown & (KEY_A | KEY_X | KEY_B)) || tap)) {
                        state_t0 -= (uint64_t)(BB_RESULTS_ANIM_MS - state_ms);
                        state_ms = BB_RESULTS_ANIM_MS;
                        kDown &= ~(u32)(KEY_A | KEY_X | KEY_B);
                        tap = false;
                    }
                    /* the ScoreScreen buttons: D-Pad moves the focus (UiButton focus sound), A presses the focused one, X = Retry, touch either */
                    if (state_ms >= BB_RESULTS_ANIM_MS) {
                        if (kDown & (KEY_DLEFT | KEY_DRIGHT)) { results_focus ^= 1; bb_uisfx_play(BB_UISFX_TOUCH); }
                        u32 act = 0;
                        if (kDown & KEY_A) act = results_focus ? KEY_X : KEY_A;
                        if (kDown & KEY_X) act = KEY_X;
                        if (tap) { const int h = bb_results_hit(tap_x, tap_y, (int64_t)state_ms); if (h >= 0) { results_focus = h; act = h ? KEY_X : KEY_A; } }
                        kDown = (kDown & ~(u32)(KEY_A | KEY_X)) | act;
                    }
                }
                if (state == APP_GAMEOVER) {
                    if (tap) {
                        const int z = bb_ui_zone_hit(tap_x, tap_y);
                        if (z == BB_ZONE_FAIL || z == BB_ZONE_FAIL + 1) { if (z - BB_ZONE_FAIL == fail_focus) kDown |= KEY_A; else { fail_focus = z - BB_ZONE_FAIL; bb_uisfx_play(BB_UISFX_TOUCH); } }
                    }
                    if (kDown & (KEY_DLEFT | KEY_DRIGHT | KEY_CPAD_LEFT | KEY_CPAD_RIGHT)) { fail_focus ^= 1; bb_uisfx_play(BB_UISFX_TOUCH); }
                    if ((kDown & KEY_A) && fail_focus == 1) kDown = (kDown & ~(u32)KEY_A) | KEY_B;   /* A on Give Up */
                }
                if (state == APP_GAMEOVER && (kDown & KEY_A)) bb_uisfx_play(BB_UISFX_ACCEPT);       /* GameOver._on_retry_button_up */
                if (state == APP_GAMEOVER && (kDown & KEY_B)) {   /* _on_giveup_button_up: ui_back, Save.give_exp(-10) */
                    bb_uisfx_play(BB_UISFX_BACK);
                    if (!g_core.autoplay && !smoke) { bb_run_gave_up(&g_save); save_now("gave up"); }
                }
                if (state == APP_RESULTS && (kDown & KEY_A) && g_powerup) {   /* ScoreScreen continue -> remove_powerups (Retry keeps it) */
                    if (bb_powerup_consume(&g_save, g_powerup) && !smoke) save_now("power-up used");
                    g_powerup = 0;
                }
                if (kDown & ((state == APP_RESULTS) ? KEY_X : KEY_A)) {
                    bb_uisfx_stop_long();
                    start_stage(stage, diff, opt_no_fail, opt_autoplay);
                    prev_lanes = lanes;
                    smoke_cursor = 0;
                    ENTER(APP_PLAYING);
                } else if (kDown & ((state == APP_RESULTS) ? KEY_A : KEY_B)) {
                    bb_uisfx_stop_long();
                    /* ScoreScreen._on_continue_button_up: the post-level cutscene plays after a pass unless skipped */
                    if (state == APP_RESULTS && !g_core.autoplay && !smoke && !g_save.settings.skip_cutscene && open_cutscene(stage, "post")) {
                        cs_after = 1;
                        ENTER(APP_CUTSCENE);
                    } else {
                        ENTER_LEVELS();
                    }
                }
                if (smoke_fail && state == APP_GAMEOVER && state_ms > 2700) {
                    bb_diag_log("SMOKE END state=%s score=%ld life=%ld missed=%lu absent=%lu", app_state_name(state), (long)g_core.stats.score,
                                (long)g_core.stats.health, (unsigned long)g_core.stats.missed, (unsigned long)g_core.stats.absent);
                    goto quit;
                }
                if (smoke && !smoke_fail && !smoke_stress && state == APP_RESULTS &&
                    state_ms > (uint64_t)BB_RESULTS_ANIM_MS + (smoke_results ? 5800u : 1500u)) {   /* results: both banners in and out */
                    if (smoke_progress) progress_log("after");
                    bb_diag_log("SMOKE END state=%s score=%ld grade=%s hit=%lu reg=%lu missed=%lu absent=%lu maxcombo=%lu",
                                app_state_name(state), (long)g_core.stats.score, bb_grade_name(g_core.stats.grade),
                                (unsigned long)g_core.stats.hit, (unsigned long)g_core.stats.registered,
                                (unsigned long)g_core.stats.missed, (unsigned long)g_core.stats.absent,
                                (unsigned long)g_core.stats.max_combo);
                    goto quit;
                }
                break;
            }
        }

        if (!smoke && pre_state == state) {
            switch (state) {
                case APP_MENU_MAIN: if (cursor != pre_cursor) bb_uisfx_play(BB_UISFX_TOUCH); break;
                case APP_MENU_LEVELS: if (cursor != pre_cursor || cur_act != pre_act) bb_uisfx_play(BB_UISFX_TICK); break;
                case APP_MENU_DIFF: if (diff != pre_diff) bb_uisfx_play(BB_UISFX_TICK); break;
                case APP_MENU_MODS: if (mod_cursor != pre_mod) bb_uisfx_play(BB_UISFX_TOUCH); break;
                case APP_MENU_SETTINGS: if (settings_cursor != pre_set || opt_cat != pre_cat || opt_page != pre_page) bb_uisfx_play(BB_UISFX_TOUCH); break;
                case APP_PAUSED: if (pause_cursor != pre_pause) bb_uisfx_play(BB_UISFX_TOUCH); break;
                default: break;
            }
        }

        if (want_launch && !smoke && !launch_go && !launch_skip_cs) {
            launch_go = true;                 /* 0.30 s: the scene music fades out and the screen dips to black, then the real launch below runs */
            launch_t0 = osGetTime();
            want_launch = false;
        }
        if (launch_go) {
            float lp = (float)(osGetTime() - launch_t0) / 300.0f;
            if (g_audio.ndsp_available) {
                float mix[12];
                memset(mix, 0, sizeof(mix));
                mix[0] = mix[1] = (1.0f - (lp > 1.0f ? 1.0f : lp)) * (float)g_save.settings.master_vol / 10.0f * (float)g_save.settings.music_vol / 10.0f;
                ndspChnSetMix(BB_AUDIO_CHANNEL, mix);
            }
            if (lp >= 1.0f) {
                launch_go = false;
                want_launch = true;
            }
        }
        if (want_launch && launch_kind == 1) {
            want_launch = false;
            launch_kind = 0;
            g_menu_track = NULL;
            bb_screens_close(&g_scr);   /* frees the reel preview (it was still drawn during the dip) */
            if (open_cutscene(theater_stage, theater_post ? "post" : "pre")) { cs_after = 4; ENTER(APP_CUTSCENE); }
            else { bb_screens_open(&g_scr, SC_EXTRAS); g_scr.node = SCN_THEATER; ENTER(APP_SCREEN); }
        }
        if (want_launch) {
            want_launch = false;
            g_menu_track = NULL;   /* the stage song replaces the scene music */
            if (!launch_skip_cs && !smoke && !g_save.settings.skip_cutscene && open_cutscene(stage, "pre")) {
                cs_after = 0;
                ENTER(APP_CUTSCENE);
            } else {
                launch_skip_cs = false;
                ENTER(APP_LOADING);   /* the loading screen shows first; the stage loads under its last frame */
            }
        }

        bb_view_tick(&g_view, &g_core);
        bb_view_set_held_lanes(state == APP_PLAYING && !g_core.autoplay ? lanes : 0);
        {
            const bool book = mod_legacy_layout && stage_is_legacy(stage);
            bb_view_set_scroll_mode(book ? BB_SCROLL_SIDE : g_save.settings.scroll_mode);
            bb_view_set_book(book, book ? g_stages[stage].art_u0 : 0.0f, book ? g_stages[stage].art_u1 : 1.0f);
        }

        /* ===================== frame ===================== */
        uint64_t wait_t0 = svcGetSystemTick();
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        wait_ms = (double)(svcGetSystemTick() - wait_t0) / ((double)SYSCLOCK_ARM11 / 1000.0);
        bb_cs_flush();
        C2D_TextBufClear(text_buf);

        /* ---- top screen ---- */
        C2D_TargetClear(top, C2D_Color32(0x00, 0x00, 0x00, 0xFF));
        C2D_SceneBegin(top);
        float boot_black = 0.0f;   /* boot screens: fade in from black over BB_BOOT_FADE_MS, out to black when leaving */
        if (state == APP_TITLE) {
            const float fin = 1.0f - bb_ui_ease_out((float)state_ms / (float)BB_BOOT_FADE_MS);
            const float fout = boot_leave ? bb_ui_ease_out((float)(osGetTime() - boot_leave) / (float)BB_BOOT_FADE_MS) : 0.0f;
            boot_black = fin > fout ? fin : fout;
        }
        /* Extras, Achievements and Options hold the main menu's top screen: no fade or logo entry while moving between them */
        static bool logo_held = false;
        const bool logo_top = (state == APP_SCREEN && (g_scr.node == SCN_EXTRAS || g_scr.node == SCN_ACH)) || (state == APP_MENU_SETTINGS && !settings_from_pause) ||
                              state == APP_MODMGR;
        if (logo_top) logo_held = true;
        else if (state != APP_MENU_MAIN) logo_held = false;
        bool in_game = state == APP_PLAYING || state == APP_PAUSED || state == APP_FAIL_TAPE || state == APP_GAMEOVER ||
                       state == APP_OUTRO || state == APP_RESULTS || state == APP_COUNTIN;
        if (in_game) {
            bb_view_draw_top(&g_view, &g_core);
            if (state == APP_COUNTIN) bb_flow_countin((float)state_ms / 1000.0f);
            if (state == APP_PAUSED) bb_flow_pause_top((int64_t)state_ms, 1.0f);
            if (state == APP_FAIL_TAPE || state == APP_GAMEOVER) bb_flow_fail_top((int64_t)state_ms + (state == APP_GAMEOVER ? 2000 : 0), caption);
            if (state == APP_RESULTS) bb_results_draw_top(&g_res, (int64_t)state_ms);
        } else if (state == APP_MENU_SETTINGS && settings_from_pause) {   /* the paused snapshot stays on top */
            bb_view_draw_top(&g_view, &g_core);
            bb_flow_pause_top(1000, 1.0f);
        } else if (state == APP_LOADING) {
            bb_ui_logo_top();
        } else if (state == APP_CUTSCENE) {
            bb_cs_draw(text_buf);
        } else if (state == APP_SCREEN) {
            bb_screens_draw(&g_scr, text_buf);
        } else if (state == APP_MENU_SETTINGS || state == APP_MODMGR || (state == APP_MENU_MAIN && logo_held)) {
            bb_ui_logo_top_still();   /* Options (and the way back to the main menu) keep the pulsing logo, no transition */
        } else {
            const char* labels[MAX_STAGES + 8];
            const char* values[MAX_STAGES + 8];
            u32 colors[MAX_STAGES + 8];
            char lbuf[MAX_STAGES + 8][64];
            const u32 cw = C2D_Color32(0xE8, 0xE8, 0xF0, 0xFF);
            if (state == APP_CALIB) {
                const float bps = 100.0f / 60.0f;   /* calibrator_jam.ogg is 100 BPM */
                const float pos = (float)calib_pos_us / 1e6f * bps;
                const int beat = menu_music_ok ? (int)pos : -1;
                bb_ui_calibration(text_buf, g_save.settings.calibration_ms, beat, (pos - (float)beat) / bps);
            } else if (state == APP_TITLE) {
                if (boot_step == 2 && g_intro_ok) bb_video_draw(&g_intro, (int64_t)state_ms * 1000, 0.40f);
                bb_boot_top(boot_step, boot_black);
                if (g_egg_on) bb_video_draw(&g_egg, (int64_t)(osGetTime() - g_egg_t0) * 1000, 0.96f);   /* keyed over the warning */
            } else if (state == APP_MENU_MAIN) {
                bb_ui_main_menu(text_buf, cursor, bb_rank_from_exp(g_save.player.exp), (int)g_save.player.money, (int)g_save.player.exp);
            } else if (state == APP_MENU_LEVELS) {
                int rows[MAX_STAGES];
                const int n = level_rows(cur_act, rows);
                if (n > 0) {
                    const stage_info_t* si = &g_stages[rows[cursor < n ? cursor : n - 1]];
                    bb_ui_level_card(si->id, si->card_img, stage_open(rows[cursor < n ? cursor : n - 1]), cur_act, g_act_count);
                }
            } else if (state == APP_MENU_DIFF) {
                bb_ui_difficulty_top(text_buf);
            } else if (state == APP_MENU_MODS) {
                bb_ui_modifiers_top(text_buf);
            } else if (state == APP_MENU_CONFIRM_RESET) {
                labels[0] = "No, keep my data"; labels[1] = "Yes, erase scores and settings";
                values[0] = values[1] = "";
                colors[0] = colors[1] = cw;
                (void)lbuf;
                bb_view_draw_menu(text_buf, "RESET SAVE DATA?", "This cannot be undone", labels, values, colors, 2, confirm_cursor, "A: confirm    B: cancel");
            }
        }

        const bool paused_settings = state == APP_MENU_SETTINGS && settings_from_pause;
        if (!in_game && state != APP_CUTSCENE && state != APP_TITLE && state != APP_LOADING && !paused_settings) bb_ui_vignette();
        if (!in_game && state != APP_CUTSCENE && state != APP_TITLE && !paused_settings && !logo_held) bb_ui_draw_fade();   /* (also the loading screen) */
        {   /* top screen: loading fade-out / stage fade-in (see the bottom-screen twin below) */
            float a = 0.0f;
            if (state == APP_LOADING && state_ms > LOAD_OUT_MS) a = bb_ui_ease_out((float)(state_ms - LOAD_OUT_MS) / (float)LOAD_FADE_MS);
            if (state == APP_PLAYING && play_fade_t0 && osGetTime() - play_fade_t0 < 300) a = 1.0f - bb_ui_ease_out((float)(osGetTime() - play_fade_t0) / 300.0f);
            if (a > 0.003f) C2D_DrawRectSolid(0, 0, 0.98f, 400, 240, C2D_Color32(0, 0, 0, (u8)(255.0f * (a > 1.0f ? 1.0f : a))));
        }
        if (launch_go) {
            float lp = (float)(osGetTime() - launch_t0) / 300.0f;
            C2D_DrawRectSolid(0, 0, 0.96f, 400, 240, C2D_Color32(0, 0, 0, (u8)(255.0f * (lp > 1.0f ? 1.0f : bb_ui_ease_out(lp)))));
        }   /* every menu fades in from black on entry */

        /* ---- bottom screen ---- */
        C2D_TargetClear(bot, C2D_Color32(0x00, 0x00, 0x00, 0xFF));
        C2D_SceneBegin(bot);
        bb_ui_zone_clear();               /* touch zones are re-registered by this frame's bottom-screen draw */
        bb_view_set_system_text(true);   /* bottom screen keeps its original typography */
        if (state == APP_SCREEN && g_scr.node != SCN_POWERUP) {
            bb_view_set_system_text(false);   /* shop, extras, gallery, theater, characters, collection, achievements: the menu half */
            bb_screens_draw_bottom(&g_scr, text_buf);
            bb_ui_draw_fade();
            bb_view_set_system_text(true);
        } else if (state == APP_FAIL_TAPE || state == APP_GAMEOVER) {   /* level failed: the frozen highway, then the game-over art and buttons */
            bb_view_draw_bottom(&g_view, &g_core, text_buf, g_save.settings.control_mode, bb_scroll_multiplier(g_save.settings.scroll_idx));
            bb_view_set_system_text(false);
            bb_flow_fail_bottom((int64_t)state_ms + (state == APP_GAMEOVER ? 2000 : 0), fail_focus, caption);
            bb_view_set_system_text(true);
        } else if (state == APP_PLAYING || state == APP_PAUSED || state == APP_OUTRO || state == APP_COUNTIN) {
            bb_view_draw_bottom(&g_view, &g_core, text_buf, g_save.settings.control_mode, bb_scroll_multiplier(g_save.settings.scroll_idx));
            if (state == APP_PAUSED || state == APP_COUNTIN) {   /* the pause menu lives on the touch screen; it fades out during the countdown */
                char pline[96], dn[16];
                snprintf(dn, sizeof(dn), "%s", k_diff_name[diff]);
                dn[0] = (char)toupper((unsigned char)dn[0]);
                snprintf(pline, sizeof(pline), "%s - %s", g_stages[stage].title, dn);
                bb_view_set_system_text(false);
                bb_flow_pause_bottom(pause_cursor, pline, state == APP_PAUSED ? (int64_t)state_ms : 1000,
                                     state == APP_PAUSED ? 1.0f : bb_flow_countin_menu_alpha((float)state_ms / 1000.0f));
                bb_view_set_system_text(true);
            }
        } else if (state == APP_LOADING) {   /* loading: the level's framed shot alone, with the spinner */
            bb_view_set_system_text(false);
            bb_ui_loading_bottom(g_stages[stage].id, g_stages[stage].card_img);
            bb_ui_draw_fade();
            bb_view_set_system_text(true);
        } else if (state == APP_RESULTS) {   /* the original ScoreScreen fades its black backing in over the frozen game */
            bb_view_draw_bottom(&g_view, &g_core, text_buf, g_save.settings.control_mode, bb_scroll_multiplier(g_save.settings.scroll_idx));
            bb_view_set_system_text(false);
            bb_results_draw(&g_res, (int64_t)state_ms, results_focus);
            bb_view_set_system_text(true);
        } else if (state == APP_MENU_LEVELS || state == APP_MENU_DIFF || state == APP_MENU_MODS) {   /* these menus live on the touch screen */
            bb_view_set_system_text(false);
            if (state == APP_MENU_LEVELS) draw_levels_bottom(text_buf, cur_act, cursor);
            else if (state == APP_MENU_DIFF) draw_diff_bottom(text_buf, stage, diff);
            else bb_ui_modifiers(text_buf, mod_cursor, bb_scroll_multiplier(g_save.settings.scroll_idx), mod_no_hud, mod_no_beat_bar,
                                 stage_is_legacy(stage) ? (int)mod_legacy_layout : -1);
            bb_ui_draw_fade();
            bb_view_set_system_text(true);
        } else if (state == APP_MENU_SETTINGS && settings_from_pause) {   /* settings over the paused highway, like the pause menu */
            bb_view_draw_bottom(&g_view, &g_core, text_buf, g_save.settings.control_mode, bb_scroll_multiplier(g_save.settings.scroll_idx));
            bb_view_set_system_text(false);
            bb_ui_settings_on_bottom(true);
            draw_settings_ui(text_buf, opt_page, opt_cat, settings_cursor, ((kRep & KEY_DDOWN) ? 1 : 0) - ((kRep & KEY_DUP) ? 1 : 0));
            bb_ui_settings_on_bottom(false);
            bb_view_set_system_text(true);
        } else if (state == APP_TITLE) {   /* boot sequence, bottom half */
            bb_view_set_system_text(false);
            if (g_egg_on) C2D_DrawRectSolid(0, 0, 0.99f, 320, 240, C2D_Color32(0, 0, 0, 0xFF));   /* the easter egg: bottom screen black, no touch zones */
            else bb_boot_bottom(boot_step, boot_black);
            bb_view_set_system_text(true);
        } else if (state == APP_MODMGR) {   /* Mods: hub, library, a mod's screen */
            bb_view_set_system_text(false);
            const int mod_cap = MAX_STAGES - (g_stage_count - g_mod_count);   /* the list is shared with the built-in stages */
            if (mm_page == 0) bb_mods_hub_bottom(mm_hub, g_mod_count, g_mod_count + g_mod_overflow, mod_cap);
            else if (mm_page == 1) bb_mods_library_bottom(g_mod_entries, g_mod_count, mm_lib, g_mod_count + g_mod_overflow, mod_cap);
            else {
                int k = 0;
                for (int i = 0; i < g_mod_count; i++) if (g_mod_rows[i] == stage) k = i;
                if (g_mod_count > 0) bb_mods_detail_bottom(&g_mod_entries[k], k, mm_armed);
            }
            bb_ui_draw_fade();
            bb_view_set_system_text(true);
        } else if (state == APP_MENU_SETTINGS) {   /* Options on the touch screen, over the dimmed main-menu video */
            bb_view_set_system_text(false);
            bb_ui_main_menu_bottom(main_cursor, 255, false);
            C2D_DrawRectSolid(0, 0, 0.30f, 320, 240, C2D_Color32(0, 0, 0, 0xA5));   /* dimmed under the dialog (not over it) */
            bb_ui_settings_on_bottom(true);
            draw_settings_ui(text_buf, opt_page, opt_cat, settings_cursor, ((kRep & KEY_DDOWN) ? 1 : 0) - ((kRep & KEY_DUP) ? 1 : 0));
            bb_ui_settings_on_bottom(false);
            bb_ui_draw_fade();
            bb_view_set_system_text(true);
        } else if (state == APP_CUTSCENE) {
            bb_view_set_system_text(false);
            bb_ui_cutscene_bottom(bb_cs_autoplay());
            bb_ui_draw_fade();
            bb_view_set_system_text(true);
        } else if (state == APP_SCREEN && g_scr.node == SCN_POWERUP) {
            bb_view_set_system_text(false);
            bb_screens_draw_powerup_bottom(&g_scr, text_buf);
            bb_ui_draw_fade();
            bb_view_set_system_text(true);
        } else if (state == APP_MENU_MAIN) {
            /* main menu: the original menu video with the entries (touch targets); behind the achievements drawer it stays, dimmed */
            bb_ui_main_menu_bottom(cursor, state == APP_MENU_MAIN ? 255 : 110, state == APP_MENU_MAIN);
            bb_ui_draw_fade();
        } else {
            /* companion.c: the bottom screen outside gameplay (3DS adaptation; bb_view_draw_bottom_blank() restores the black screen) */
            bb_comp_t comp;
            bb_comp_clear(&comp, BB_COMP_BLANK);
            fill_companion(&comp, state, cursor, cur_act, stage, diff, opt_page, opt_cat, settings_cursor, mod_no_hud, mod_no_beat_bar, menu_music_ok);
            bb_comp_draw(&comp);
        }
        {   /* loading screen fade-out and the stage's fade-in from black, both screens (drawn last on the bottom; the top below) */
            float a = 0.0f;
            if (state == APP_LOADING && state_ms > LOAD_OUT_MS) a = bb_ui_ease_out((float)(state_ms - LOAD_OUT_MS) / (float)LOAD_FADE_MS);
            if (state == APP_PLAYING && play_fade_t0 && osGetTime() - play_fade_t0 < 300) a = 1.0f - bb_ui_ease_out((float)(osGetTime() - play_fade_t0) / 300.0f);
            if (a > 0.003f) C2D_DrawRectSolid(0, 0, 0.98f, 320, 240, C2D_Color32(0, 0, 0, (u8)(255.0f * (a > 1.0f ? 1.0f : a))));
        }
        if (launch_go) {   /* level start: both screens fade to black together */
            float lp = (float)(osGetTime() - launch_t0) / 300.0f;
            C2D_DrawRectSolid(0, 0, 0.97f, 320, 240, C2D_Color32(0, 0, 0, (u8)(255.0f * (lp > 1.0f ? 1.0f : bb_ui_ease_out(lp)))));
        }
        if (show_diag) {
            for (int i = 0; i < diag_n; i++) {
                bb_view_text(text_buf, 4.0f, 4.0f + (float)i * 12.0f, 0.36f, C2D_Color32(0x90, 0xFF, 0x90, 0xFF), C2D_AlignLeft, diag_lines[i], 0.8f);
            }
        }
        bb_view_set_system_text(false);
        C3D_FrameEnd(0);

        /* ===================== diagnostics snapshot (throttled) ===================== */
        if (frame_count % 10 == 0) {
            memset(&snap, 0, sizeof(snap));
            snap.app_state = (int)state;
            snap.state_name = app_state_name(state);
            snap.ndsp_active = g_audio.ndsp_available;
            snap.wav_streaming = g_audio.using_wav_file;
            snap.sample_rate = g_audio.sample_rate;
            snap.consumed_samples = g_audio.max_consumed_samples;
            snap.bufs = g_audio.active_queued_bufs;
            snap.underruns = g_audio.underrun_count;
            snap.dropped = g_audio.dropped_frames;
            snap.audio_pos_us = audio_pos_us;
            snap.chart_time_us = g_core.song_cur_time_us;
            snap.calibration_us = g_core.calibration_offset_us;
            snap.score = g_core.stats.score;
            snap.health = g_core.stats.health;
            snap.combo = g_core.stats.combo;
            snap.perfect = g_core.stats.perfect;
            snap.great = g_core.stats.great;
            snap.nice = g_core.stats.nice;
            snap.meh = g_core.stats.meh;
            snap.missed = g_core.stats.missed;
            snap.absent = g_core.stats.absent;
            snap.registered = g_core.stats.registered;
            snap.last_judgment = bb_judgment_name(g_core.stats.last_judgment);
            snap.last_delta_ms = (double)g_core.stats.last_hit_delta_us / 1000.0;
            snap.frame_ms_avg = frame_avg;
            snap.frame_ms_max = frame_max;
            snap.sheet_loads = g_anim.loads;
            snap.last_load_ms = g_anim.last_load_ms;
            snap.resident_kib = g_anim.resident_bytes / 1024U;
            snap.loads_failed = g_view.sheet_loads_failed;
            snap.loop_idx = g_core.current_loop_idx;
            snap.shutters = g_core.shutter_fired_count;
            snap.cpu_pct = (double)C3D_GetProcessingTime() * 6.0;
            snap.gpu_pct = (double)C3D_GetDrawingTime() * 6.0;
            snap.linear_free_kib = (uint32_t)(linearSpaceFree() / 1024);
            snap.control_mode = g_save.settings.control_mode;
            if (show_diag) diag_n = bb_diag_lines(&snap, diag_lines, 12);
        }

        if (frame_count % 120 == 0) {
            bb_diag_log("t=%+.2fs chart=%+.2fs st=%s score=%ld combo=%lu P%lu G%lu N%lu M%lu miss%lu abs%lu life=%ld loop=%lu frame=%.1fms max=%.1fms busy=%.1f/%.1fms cpu=%.0f%% gpu=%.0f%% "
                        "ndsp=%d und=%lu samp=%llu astep_max=%.1fms astall=%lu/%lu lin=%luKiB heap=%luKiB sheetfail=%lu lastload=%lums",
                        (double)audio_pos_us / 1e6, (double)g_core.song_cur_time_us / 1e6, app_state_name(state), (long)g_core.stats.score,
                        (unsigned long)g_core.stats.combo, (unsigned long)g_core.stats.perfect, (unsigned long)g_core.stats.great,
                        (unsigned long)g_core.stats.nice, (unsigned long)g_core.stats.meh, (unsigned long)g_core.stats.missed,
                        (unsigned long)g_core.stats.absent, (long)g_core.stats.health, (unsigned long)g_core.current_loop_idx,
                        frame_avg, frame_max, busy_avg, busy_win_max, (double)C3D_GetProcessingTime() * 6.0, (double)C3D_GetDrawingTime() * 6.0,
                        g_audio.ndsp_available, (unsigned long)g_audio.underrun_count, (unsigned long long)g_audio.max_consumed_samples,
                        astep_max_ms, (unsigned long)astall_frames, (unsigned long)aplay_frames, (unsigned long)(linearSpaceFree() / 1024),
                        (unsigned long)bb_hw_heap_free_kib(), (unsigned long)g_view.sheet_loads_failed, (unsigned long)g_anim.last_load_ms);
            busy_win_max = 0.0;
        }

        /* Evidence screenshots in smoke mode (chart time based; each capture costs ~120 ms) */
        if (smoke && !smoke_stress && !smoke_short && !smoke_perf && !smoke_progress && (state == APP_PLAYING || state == APP_OUTRO)) {
            double ct = (double)g_core.song_cur_time_us / 1e6;
            if (!snap_a && ct > 5.0)   { bb_diag_capture("play_5s");   snap_a = true; }
            if (!snap_b && ct > 33.0)  { bb_diag_capture("play_33s");  snap_b = true; }
            if (!snap_c && ct > 62.0)  { bb_diag_capture("play_62s");  snap_c = true; }
            if (!snap_d && ct > 100.0 && ct < 118.0) { bb_diag_capture("play_100s"); snap_d = true; }
            if (!snap_v && g_video_count > 0 && g_core.song_cur_time_us > g_video[g_video_count - 1].start_us + 1500000) {
                bb_diag_capture("video");
                snap_v = true;
            }
        }
        if (smoke_psettings) {
            static int ps = 0;
            static uint64_t pt = 0;
            const uint64_t now = osGetTime();
            if (ps == 0 && state == APP_PLAYING && g_core.song_cur_time_us > 5000000) { g_apt_interrupted = true; ps = 1; pt = now; }
            else if (ps == 1 && state == APP_PAUSED && now - pt > 700) { pause_cursor = 2; drive_key = KEY_A; ps = 2; pt = now; }
            else if (ps == 2 && state == APP_MENU_SETTINGS && now - pt > 700) { bb_diag_capture("psettings_menu"); smoke_tx = 95 - 40 + 52; smoke_ty = 72 + 8; ps = 3; pt = now; }   /* tap Audio (selected) */
            else if (ps == 3 && now - pt > 700) { bb_diag_capture("psettings_audio"); bb_diag_log("pausesettings: page=%d", opt_page); drive_key = KEY_B; ps = 4; pt = now; }
            else if (ps == 4 && now - pt > 500) { drive_key = KEY_B; ps = 5; pt = now; }
            else if (ps == 5 && now - pt > 700) {
                bb_diag_log("SMOKE END pausesettings state=%s chart=%+.3fs", app_state_name(state), (double)g_core.song_cur_time_us / 1e6);
                goto quit;
            }
        }
        if (smoke_pause) {   /* auto-pause as after the HOME Menu, capture, RESUME, count-in capture, then ~1.5 s of play */
            static int pstage = 0;
            static int64_t resumed_at = 0;
            if (pstage == 0 && state == APP_PLAYING && g_core.song_cur_time_us > 6000000) {
                g_apt_interrupted = true;   /* same path the APT hook takes */
                pstage = 1;
            } else if (pstage == 1 && state == APP_PAUSED && state_ms > 900) {
                bb_diag_capture("pause");
                bb_diag_log("pause smoke: paused at chart=%+.3fs (auto-pause)", (double)g_core.song_cur_time_us / 1e6);
                resumed_at = g_core.song_cur_time_us;
                pause_cursor = 0;
                drive_key = KEY_A;
                pstage = 2;
            } else if (pstage == 2 && state == APP_COUNTIN && state_ms > 450) {
                bb_diag_capture("countin");
                bb_diag_log("pause smoke: countdown chart=%+.3fs (must equal the paused time)", (double)g_core.song_cur_time_us / 1e6);
                pstage = 3;
            } else if (pstage == 3 && state == APP_PLAYING && g_core.song_cur_time_us > resumed_at + 1500000) {
                bb_diag_log("SMOKE END pause stage=%s resumed_from=%+.3fs now=%+.3fs registered=%lu", g_stages[stage].id, (double)resumed_at / 1e6,
                            (double)g_core.song_cur_time_us / 1e6, (unsigned long)g_core.stats.registered);
                goto quit;
            }
        }
        if (smoke_short && state == APP_PLAYING) {
            double ct = (double)g_core.song_cur_time_us / 1e6;
            if (!snap_a && ct > 6.0)  { bb_diag_capture("short_6s");  snap_a = true; }
            if (!snap_b && ct > 14.0) {
                bb_diag_capture("short_14s");
                bb_diag_log("SMOKE END (short) stage=%s diff=%s chart=%+.1fs notes_registered=%lu score=%ld sheetfail=%lu", g_stages[stage].id,
                            k_diff_name[diff], ct, (unsigned long)g_core.stats.registered, (long)g_core.stats.score, (unsigned long)g_view.sheet_loads_failed, (unsigned long)g_anim.last_load_ms);
                goto quit;
            }
        }
        if (smoke_menus && state_ms > (state == APP_TITLE ? 1800u : 500u)) {
            static const char* nm[6] = { "menu_main", "menu_levels", "menu_diff", "menu_settings", "menu_confirm", "menu_mods" };
            const bool was_title = state == APP_TITLE;
            if (was_title) { bb_diag_capture("menu_title"); ENTER(APP_MENU_MAIN); }
            else if (state == APP_MENU_SETTINGS && opt_page == 4) bb_diag_capture("menu_credits");
            else if (state == APP_MENU_SETTINGS && opt_page >= 0) bb_diag_capture("menu_settings_audio");
            else if (state <= APP_MENU_MODS) bb_diag_capture(nm[state]);
            if (was_title) { }   /* the main menu is captured on its own next pass */
            else if (state == APP_MENU_MAIN) { ENTER(APP_MENU_LEVELS); }
            else if (state == APP_MENU_LEVELS) { cur_act = 1; cursor = 0; ENTER(APP_MENU_DIFF); if (!smoke_stage[0]) stage = 5; }   /* "menus <stage>": that stage's difficulty and modifiers screens */
            else if (state == APP_MENU_DIFF) { ENTER(APP_MENU_MODS); }
            else if (state == APP_MENU_MODS) { ENTER(APP_MENU_SETTINGS); }
            else if (state == APP_MENU_SETTINGS && opt_page < 0) { opt_page = 0; settings_cursor = 2; bb_ui_enter(); state_t0 = osGetTime(); }
            else if (state == APP_MENU_SETTINGS && opt_page == 0) { opt_page = 4; bb_ui_enter(); state_t0 = osGetTime(); }
            else if (state == APP_MENU_SETTINGS) { ENTER(APP_MENU_CONFIRM_RESET); }
            else { bb_diag_log("SMOKE END menus"); goto quit; }
        }
        if (smoke_cutscene && state == APP_CUTSCENE) {
            int pnl = bb_cs_panel();
            if (pnl != smoke_cs_cap && (pnl == 0 || pnl == 3 || pnl == 6) && state_ms > (uint64_t)pnl * 900 + 400) {
                char nmc[24];
                snprintf(nmc, sizeof(nmc), "cs%d_p%d", cs_after - 2, pnl);
                bb_diag_capture(nmc);
                smoke_cs_cap = pnl;
            }
        }
        if (smoke_flow && state == APP_PLAYING && g_core.song_cur_time_us > 8000000) {
            bb_diag_log("SMOKE END flow stage=%s chart=%+.1fs registered=%lu score=%ld", g_stages[stage].id, (double)g_core.song_cur_time_us / 1e6,
                        (unsigned long)g_core.stats.registered, (long)g_core.stats.score);
            goto quit;
        }
        if (smoke_music) {
            if (state == APP_MENU_MAIN && state_ms > 7000) { bb_screens_open(&g_scr, SC_SHOP); ENTER(APP_SCREEN); }
            else if (state == APP_SCREEN && state_ms > 7000) {
                bb_diag_log("SMOKE END music track=%s ok=%d underruns=%lu consumed=%llu", g_menu_track ? g_menu_track : "-", menu_music_ok,
                            (unsigned long)g_audio.underrun_count, (unsigned long long)g_audio.max_consumed_samples);
                goto quit;
            }
        }
        if (smoke_shopflow) {
            static const u32 keys[] = { KEY_A, KEY_DDOWN, KEY_A, KEY_A, KEY_R, KEY_A, KEY_R, KEY_B, KEY_DDOWN, KEY_A, KEY_A, KEY_B, KEY_DDOWN, KEY_A,
                                        KEY_DRIGHT, KEY_A, KEY_DRIGHT, KEY_A, KEY_B, KEY_B };
            static const int nkeys = (int)(sizeof(keys) / sizeof(keys[0]));
            static int fstep = -1;
            static uint64_t ft0 = 0;
            const uint64_t now = osGetTime();
            if (fstep < 0 && state == APP_MENU_MAIN && state_ms > 300) {
                bb_give_money(&g_save, 3000);
                bb_give_exp(&g_save, 2600);
                bb_player_add_item(&g_save.player, 1104);
                bb_screens_open(&g_scr, SC_SHOP);
                ENTER(APP_SCREEN);
                fstep = 0;
                ft0 = now;
            } else if (fstep >= 0 && state == APP_SCREEN && now - ft0 >= 1500 + 500 * (uint64_t)fstep) {
                if (fstep > 0) {
                    bb_diag_log("shopflow after key#%d (0x%lx): node=%d cursor=%d cat=%d money=%ld outfit=%d react=%d speech='%.60s'", fstep - 1, (unsigned long)keys[fstep - 1],
                                (int)g_scr.node, g_scr.cursor[g_scr.node], g_scr.shop_cat, (long)g_save.player.money, (int)g_save.player.outfit, g_scr.react_kind, g_scr.speech);
                    if (fstep == 3) bb_diag_capture("shopflow_buy");
                    if (fstep == 11) bb_diag_capture("shopflow_talk");
                    if (fstep == 18) bb_diag_capture("shopflow_outfit");
                }
                if (fstep < nkeys) drive_key = keys[fstep++];
                else { fstep = nkeys + 1; }
            } else if (fstep >= nkeys && state == APP_MENU_MAIN) {
                bb_diag_log("SMOKE END shopflow money=%ld outfit=%d", (long)g_save.player.money, (int)g_save.player.outfit);
                goto quit;
            }
        }
        if (smoke_preview) {
            static int pstep = 0;
            if (state == APP_MENU_MAIN && state_ms > 300 && pstep == 0) { ENTER(APP_MENU_LEVELS); cursor = 0; pstep = 1; }
            else if (state == APP_MENU_LEVELS && pstep >= 1 && state_ms > 1400 + 1800u * (uint64_t)(pstep - 1)) {
                bb_diag_log("preview step %d: track=%s ok=%d underruns=%lu", pstep, g_menu_track ? g_menu_track : "-", menu_music_ok, (unsigned long)g_audio.underrun_count);
                if (pstep >= 4) { bb_diag_log("SMOKE END preview"); goto quit; }
                cursor = (cursor + 1) % 3;
                pstep++;
            }
        }
        if (smoke_calib) {
            if (state == APP_MENU_MAIN && state_ms > 300) ENTER(APP_CALIB);
            else if (state == APP_CALIB && state_ms > 2600) {
                bb_diag_capture("calib");
                bb_diag_log("SMOKE END calib music=%d pos=%.2fs", menu_music_ok, (double)calib_pos_us / 1e6);
                goto quit;
            }
        }
        if (smoke_touch) {   /* each step: one tap, then a log line of where it landed */
            static const struct { int x, y; app_state_t in; const char* cap; } k[] = {
                { 40, 24 + 3 * 28 + 10, APP_MENU_MAIN, NULL },   /* Shop: selected only (bb_ui_main_item_y) */
                { 40, 24 + 10, APP_MENU_MAIN, NULL },            /* Play: selected */
                { 40, 24 + 10, APP_MENU_MAIN, NULL },            /* Play again: opens the level select */
                { 272, 104, APP_MENU_LEVELS, "touch_levels" }, /* right neighbour: selected */
                { 160, 104, APP_MENU_LEVELS, NULL },          /* the selected portrait: opens the difficulty */
                { 296, 98, APP_MENU_DIFF, "touch_diff" },     /* Normal card (right of the carousel): selected */
                { 250, 215, APP_MENU_DIFF, NULL },            /* Modifiers */
            };
            static int tstep = 0;
            static uint64_t tt = 0;
            const uint64_t now = osGetTime();
            const int nk = (int)(sizeof(k) / sizeof(k[0]));
            if (tstep < nk && state == k[tstep].in && state_ms > 900 && now - tt > 700) {
                if (k[tstep].cap) bb_diag_capture(k[tstep].cap);
                smoke_tx = k[tstep].x; smoke_ty = k[tstep].y;
                tt = now;
                tstep++;
            } else if (tstep > 0 && now - tt > 300 && now - tt < 340) {
                bb_diag_log("touch smoke after tap %d: state=%s cursor=%d diff=%d", tstep, app_state_name(state), cursor, diff);
            }
            if (tstep >= nk && state == APP_MENU_MODS) { bb_diag_log("SMOKE END touch state=%s diff=%d", app_state_name(state), diff); goto quit; }
            if (now - tt > 8000 && tstep > 0) { bb_diag_log("SMOKE END touch STUCK at step %d state=%s", tstep, app_state_name(state)); goto quit; }
        }
        if (smoke_readkey) {
            static int rs = 0;
            static uint64_t rt = 0;
            const uint64_t now = osGetTime();
            if (rs == 0 && state == APP_MENU_MAIN && state_ms > 300) {
                progress_log("before");
                bb_screens_open(&g_scr, SC_EXTRAS); ENTER(APP_SCREEN);
                g_scr.node = SCN_COLLECT; bb_coll_open(&g_extras, &g_save);
                rs = 1; rt = now;
            } else if (rs == 1 && now - rt > 1200) {
                const bool f = bb_coll_focus_item(BB_ITEM_COLLECT_DORM_KEY);
                bb_diag_log("readkey: dorm key in the book=%d owned=%d", f, bb_player_has_item(&g_save.player, BB_ITEM_COLLECT_DORM_KEY));
                drive_key = KEY_A; rs = 2; rt = now;   /* open it (a locked one only plays ui_lock) */
            } else if (rs == 2 && now - rt > 1500) { bb_diag_capture("readkey_popup"); drive_key = KEY_B; rs = 3; rt = now; }   /* close it */
            else if (rs == 3 && now - rt > 1000) {
                int mk = -1;
                for (int i = 0; i < g_stage_count; i++) if (strcmp(g_stages[i].id, "makayla") == 0) mk = i;
                progress_log("after");
                bb_diag_log("SMOKE END readkey after_class_open=%d", mk >= 0 && stage_open(mk));
                goto quit;
            }
        }
        if (smoke_warnegg) {
            static int ws = 0;
            if (ws == 0 && state == APP_TITLE && boot_step == 0 && state_ms > 900) { drive_key = KEY_B; ws = 1; }
            else if (ws == 1 && g_egg_on && osGetTime() - g_egg_t0 > 2500) { bb_diag_capture("warnegg_2s5"); ws = 2; }
            else if (ws == 2 && g_egg_on && osGetTime() - g_egg_t0 > 5000) { bb_diag_capture("warnegg_5s"); ws = 3; }
        }
        if (smoke_boot) {
            static int bs = 0;
            static uint64_t bt = 0;
            const uint64_t now = osGetTime();
            if (bs == 0 && state == APP_TITLE && boot_step == 0 && state_ms > 120 && state_ms < 200) { bb_diag_capture("boot_warning_fadein"); bs = 1; }
            else if (bs == 1 && state_ms > 900) { bb_diag_capture("boot_warning"); drive_key = KEY_A; bs = 2; bt = now; }
            else if (bs == 2 && boot_leave && now - boot_leave > 110) { bb_diag_capture("boot_warning_fadeout"); bs = 3; }
            else if (bs == 3 && boot_step == 1 && state_ms > 900) { bb_diag_capture("boot_disclaimer"); smoke_tx = 160; smoke_ty = 120; bs = 4; bt = now; }   /* a touch continues */
            else if (bs == 4 && boot_step == 2 && state_ms > 2500) { bb_diag_capture("boot_intro"); bb_diag_log("boot smoke: intro ok=%d", g_intro_ok); bs = 5; bt = now; }
            else if (bs == 5 && state == APP_MENU_MAIN && state_ms > 800) {
                bb_diag_capture("boot_menu");
                bb_diag_log("SMOKE END boot state=%s intro_to_menu=%lums", app_state_name(state), (unsigned long)(now - bt));
                goto quit;
            }
            if (now - bt > 20000 && bs > 1 && bs < 5) { bb_diag_log("SMOKE END boot STUCK bs=%d step=%d", bs, boot_step); goto quit; }
        }
        if (smoke_modsui) {
            static int ms = 0;
            static uint64_t mt = 0;
            const uint64_t now = osGetTime();
            const int my = (int)bb_ui_main_item_y(2) + 10;
            if (ms == 0 && state == APP_MENU_MAIN && state_ms > 400) { bb_diag_capture("mods_menu"); smoke_tx = 40; smoke_ty = my; ms = 1; mt = now; }   /* select Mods */
            else if (ms == 1 && now - mt > 400) { smoke_tx = 40; smoke_ty = my; ms = 2; mt = now; }                                         /* open it */
            else if (ms == 2 && now - mt > 1200 && state == APP_MODMGR && state_ms > 1000) {   /* the hub has drawn (opening loads every thumbnail) */ bb_diag_capture("mods_hub"); bb_diag_log("mods smoke: state=%s mods=%d on_card=%d cap=%d", app_state_name(state), g_mod_count, g_mod_count + g_mod_overflow, MAX_STAGES - (g_stage_count - g_mod_count)); drive_key = KEY_A; ms = 3; mt = now; }
            else if (ms == 3 && now - mt > 1200) { bb_diag_capture("mods_library"); drive_key = KEY_DDOWN; ms = 4; mt = now; }
            else if (ms == 4 && now - mt > 300) { drive_key = KEY_DDOWN; ms = 5; mt = now; }
            else if (ms == 5 && now - mt > 900) { bb_diag_capture("mods_library2"); drive_key = KEY_A; ms = 6; mt = now; }
            else if (ms == 6 && now - mt > 1500) { bb_diag_capture("mods_detail"); bb_diag_log("mods smoke: detail stage=%s", g_stages[stage].id); drive_key = KEY_A; ms = 7; mt = now; }
            else if (ms == 7 && now - mt > 1200) { bb_diag_capture("mods_diff"); bb_diag_log("mods smoke: state=%s chart_ok=%d notes=%lu", app_state_name(state), g_chart_ok, (unsigned long)g_chart.note_count); drive_key = KEY_B; ms = 8; mt = now; }
            else if (ms == 8 && now - mt > 900) { bb_diag_log("SMOKE END modsui state=%s page=%d", app_state_name(state), mm_page); goto quit; }
        }
        if (smoke_bios) {
            static int qs = 0;
            static uint64_t qt = 0;
            const uint64_t now = osGetTime();
            if (qs == 0 && state == APP_MENU_MAIN && state_ms > 300) { bb_screens_open(&g_scr, SC_EXTRAS); g_scr.node = SCN_CHARS; g_scr.page = 1; ENTER(APP_SCREEN); qs = 1; qt = now; }
            else if (qs == 1 && now - qt > 1200) { bb_diag_capture("bios_0"); smoke_tx = 160; smoke_ty = 100; qs = 2; qt = now; }
            else if (qs == 2 && now - qt > 80) { bb_diag_capture("bios_pulse"); qs = 3; qt = now; }
            else if (qs == 3 && now - qt > 900) { bb_diag_capture("bios_1"); smoke_tx = 160; smoke_ty = 100; qs = 4; qt = now; }
            else if (qs == 4 && now - qt > 900) { bb_diag_capture("bios_2"); drive_key = KEY_R; qs = 5; qt = now; }
            else if (qs == 5 && now - qt > 900) { bb_diag_capture("bios_next"); bb_diag_log("SMOKE END bios page=%d", g_scr.page); goto quit; }
        }
        if (smoke_soundtest || smoke_sceneview) {
            static int vs = 0;
            static uint64_t vt = 0;
            const uint64_t now = osGetTime();
            const char* tag = smoke_soundtest ? "music" : "sv";
            char nm[32];
            if (vs == 0 && state == APP_MENU_MAIN && state_ms > 300) {
                bb_screens_open(&g_scr, SC_EXTRAS); ENTER(APP_SCREEN);
                g_scr.cursor[SCN_EXTRAS] = smoke_soundtest ? 0 : 4;
                drive_key = KEY_A; vs = 1; vt = now;
            } else if (vs == 1 && now - vt > 2500) { snprintf(nm, sizeof(nm), "%s_open", tag); bb_diag_capture(nm); drive_key = smoke_soundtest ? KEY_A : KEY_DRIGHT; vs = 2; vt = now; }
            else if (vs == 2 && now - vt > 2500) { snprintf(nm, sizeof(nm), "%s_a", tag); bb_diag_capture(nm); drive_key = smoke_soundtest ? KEY_DRIGHT : KEY_Y; vs = 3; vt = now; }
            else if (vs == 3 && now - vt > 2500) {
                snprintf(nm, sizeof(nm), "%s_b", tag); bb_diag_capture(nm);
                vs = smoke_sceneview ? 4 : 13; vt = now;
            } else if (vs >= 4 && vs < 12 && now - vt > 80) { drive_key = KEY_CSTICK_DOWN; vs++; vt = now; }   /* scroll the controls */
            else if (vs == 12 && now - vt > 600) { bb_diag_capture("sv_scrolled"); vs = 13; }
            else if (vs == 13) {
                bb_diag_log("SMOKE END %s node=%d stream=%d pos=%.2fs", smoke_soundtest ? "soundtest" : "sceneview", (int)g_scr.node, (int)menu_music_ok,
                            (double)g_audio.monotonic_pos_us / 1e6);
                goto quit;
            }
        }
        if (smoke_theme) {
            static int ts = 0;
            static uint64_t tt = 0;
            const uint64_t now = osGetTime();
            if (ts == 0 && state == APP_MENU_MAIN && state_ms > 800) { bb_diag_capture("theme_menu_default"); drive_key = KEY_X; ts = 1; tt = now; }
            else if (ts == 1 && state == APP_TITLE && state_ms > 1200) { bb_diag_capture("theme_splash_1"); ts = 2; }
            else if (ts == 2 && state == APP_TITLE && state_ms > 3550) { bb_diag_capture("theme_splash_flash"); ts = 3; }
            else if (ts == 3 && state == APP_TITLE && state_ms > 5200) { bb_diag_capture("theme_splash_2"); drive_key = KEY_A; ts = 4; }
            else if (ts == 4 && state == APP_MENU_MAIN && state_ms > 1500) { bb_diag_capture("theme_menu_halloween"); drive_key = KEY_X; ts = 5; tt = now; }
            else if (ts == 5 && state == APP_TITLE && state_ms > 900) { drive_key = KEY_A; ts = 6; }
            else if (ts == 6 && state == APP_MENU_MAIN && state_ms > 1200) {
                bb_diag_capture("theme_menu_back");
                bb_diag_log("SMOKE END theme theme=%d", bb_theme_get());
                goto quit;
            }
            if (now - tt > 30000 && ts > 0) { bb_diag_log("SMOKE END theme STUCK ts=%d", ts); goto quit; }
        }
        if (smoke_boop) {
            static int bs = 0, boops = 0;
            static uint64_t bt = 0;
            const uint64_t now = osGetTime();
            if (bs == 0 && state == APP_MENU_MAIN && state_ms > 300) { bb_screens_open(&g_scr, SC_SHOP); ENTER(APP_SCREEN); g_scr.cursor[SCN_SHOP] = 3; bs = 1; bt = now; }
            else if (bs == 1 && now - bt > 1500) { bb_diag_capture("boop_menu"); drive_key = KEY_A; bs = 2; bt = now; }
            else if (bs == 2 && now - bt > 1000) { bb_diag_capture("boop_view"); bs = 3; bt = now; }
            else if (bs == 3 && now - bt > 650) {
                smoke_tx = 164; smoke_ty = 88; boops++; bt = now;   /* her nose */
                if (boops == 8) bs = 4;
            } else if (bs == 4 && now - bt > 500) {
                bb_diag_capture("boop_8");
                bb_diag_log("SMOKE END boop boops=%d honk_amount=%d photo=%d stat=%lu", boops, g_scr.honk_amount, bb_player_has_item(&g_save.player, 399),
                            (unsigned long)g_save.player.stats[BB_STAT_NOSE_HONKS]);
                goto quit;
            }
            if (bs == 3 && (boops == 2 || boops == 5) && now - bt > 300 && now - bt < 340) bb_diag_capture(boops == 2 ? "boop_2" : "boop_5");
        }
        if (smoke_gallery) {
            static int gs = 0;
            static uint64_t gt = 0;
            const uint64_t now = osGetTime();
            if (gs == 0 && state == APP_MENU_MAIN && state_ms > 300) { bb_screens_open(&g_scr, SC_EXTRAS); g_scr.cursor[SCN_EXTRAS] = 1; ENTER(APP_SCREEN); drive_key = KEY_A; gs = 1; gt = now; }
            else if (gs == 1 && now - gt > 1400) { bb_diag_capture("gal_grid"); drive_key = KEY_DRIGHT; gs = 2; gt = now; }
            else if (gs == 2 && now - gt > 900) { bb_diag_capture("gal_grid2"); drive_key = KEY_R; gs = 3; gt = now; }
            else if (gs == 3 && now - gt > 900) { bb_diag_capture("gal_tab2"); drive_key = KEY_L; gs = 4; gt = now; }
            else if (gs == 4 && now - gt > 900) { drive_key = KEY_A; gs = 5; gt = now; }
            else if (gs == 5 && now - gt > 900) { bb_diag_capture("gal_lightbox"); bb_diag_log("gallery smoke: lightbox=%d", bb_gal_lightbox_open()); drive_key = KEY_R; gs = 6; gt = now; }
            else if (gs == 6 && now - gt > 500) { drive_key = KEY_R; gs = 7; gt = now; }
            else if (gs == 7 && now - gt > 800) { bb_diag_capture("gal_zoom"); drive_key = KEY_B; gs = 8; gt = now; }
            else if (gs == 8 && now - gt > 700) { bb_diag_capture("gal_closed"); bb_diag_log("SMOKE END gallery node=%d lightbox=%d", (int)g_scr.node, bb_gal_lightbox_open()); goto quit; }
        }
        if (smoke_collect && !smoke_modsui && !smoke_boot && !smoke_bios && !smoke_gallery && !smoke_theme) {
            static int cs = 0;
            static uint64_t ct = 0;
            const uint64_t now = osGetTime();
            if (cs == 0 && state == APP_MENU_MAIN && state_ms > 300) {   /* every collectable owned (in memory only: screens smokes never save) */
                for (int i = 0; i < g_extras.collect_count; i++) bb_player_add_item(&g_save.player, g_extras.collect[i].item);
                bb_screens_open(&g_scr, SC_EXTRAS);
                ENTER(APP_SCREEN);
                g_scr.cursor[SCN_EXTRAS] = 5;
                drive_key = KEY_A; cs = 1; ct = now;
            } else if (cs == 1 && now - ct > 1500) { bb_diag_capture("coll_book"); drive_key = KEY_A; cs = 2; ct = now; }
            else if (cs == 2 && now - ct > 250) { bb_diag_capture("coll_pop_mid"); cs = 3; ct = now; }
            else if (cs == 3 && now - ct > 900) { bb_diag_capture("coll_pop"); drive_key = KEY_B; cs = 4; ct = now; }
            else if (cs == 4 && now - ct > 200) { bb_diag_capture("coll_close_mid"); cs = 5; ct = now; }
            else if (cs == 5 && now - ct > 800) { drive_key = KEY_R; cs = 6; ct = now; }
            else if (cs == 6 && now - ct > 900) {
                bb_diag_capture("coll_page2");
                bb_diag_log("SMOKE END collect node=%d popup=%d", (int)g_scr.node, (int)bb_coll_popup_open());
                goto quit;
            }
        }
        if (showcase) {   /* "showcase": a ~5 minute tour of the real game for a recording; every step waits for its screen, then acts */
            enum { SC_KEY, SC_SEED, SC_CURSOR, SC_STAGE, SC_DIFF0, SC_LAUNCH, SC_HOME, SC_NODE, SC_DONE };
            typedef struct { int state; uint32_t ms; int op; u32 arg; } sc_step_t;   /* state -1: any (time from the previous step) */
            static const sc_step_t steps[] = {
                { APP_TITLE, 15000, SC_KEY, KEY_A },         /* 18+ warning: 15 s to start the recording */
                { -1, 4500, SC_KEY, KEY_A },                 /* disclaimer; the Bunfan intro plays by itself */
                { APP_MENU_MAIN, 1500, SC_SEED, 0 },
                { -1, 1200, SC_KEY, KEY_DDOWN }, { -1, 900, SC_KEY, KEY_DDOWN }, { -1, 900, SC_KEY, KEY_DDOWN },
                { -1, 1300, SC_KEY, KEY_DUP }, { -1, 500, SC_KEY, KEY_DUP }, { -1, 500, SC_KEY, KEY_DUP },
                { -1, 900, SC_KEY, KEY_A },                  /* Play */
                { APP_MENU_LEVELS, 1800, SC_KEY, KEY_DRIGHT }, { -1, 1500, SC_KEY, KEY_DRIGHT }, { -1, 1500, SC_KEY, KEY_DRIGHT },
                { -1, 1500, SC_KEY, KEY_DRIGHT }, { -1, 1800, SC_KEY, KEY_R },   /* Volume 2 */
                { -1, 1800, SC_STAGE, 0 },                   /* In The Dorms */
                { -1, 1800, SC_KEY, KEY_A },
                { APP_MENU_DIFF, 300, SC_DIFF0, 0 }, { -1, 1100, SC_KEY, KEY_DRIGHT }, { -1, 1100, SC_KEY, KEY_DRIGHT },   /* Expert */
                { -1, 1600, SC_LAUNCH, 0 },                  /* hands-free */
                { APP_CUTSCENE, 22000, SC_KEY, KEY_B },      /* some of the pre-level scene, then skip */
                { APP_RESULTS, BB_RESULTS_ANIM_MS + 4500, SC_KEY, KEY_A },
                { APP_MENU_LEVELS, 1500, SC_KEY, KEY_B },
                { APP_MENU_MAIN, 1500, SC_CURSOR, 3 }, { -1, 700, SC_KEY, KEY_A },   /* Shop */
                { APP_SCREEN, 3500, SC_NODE, 1 }, { -1, 3500, SC_NODE, 2 }, { -1, 3500, SC_NODE, 3 }, { -1, 3500, SC_HOME, 0 },
                { APP_MENU_MAIN, 1200, SC_CURSOR, 4 }, { -1, 700, SC_KEY, KEY_A },   /* Extras */
                { APP_SCREEN, 3000, SC_NODE, 10 }, { -1, 4000, SC_NODE, 11 }, { -1, 3500, SC_NODE, 12 }, { -1, 4000, SC_NODE, 13 },
                { -1, 3500, SC_HOME, 0 },
                { APP_MENU_MAIN, 1000, SC_CURSOR, 5 }, { -1, 700, SC_KEY, KEY_A },   /* Achievements */
                { APP_SCREEN, 4500, SC_HOME, 0 },
                { APP_MENU_MAIN, 1000, SC_CURSOR, 2 }, { -1, 700, SC_KEY, KEY_A },   /* Mods */
                { APP_MODMGR, 3000, SC_KEY, KEY_A }, { -1, 2000, SC_KEY, KEY_DDOWN }, { -1, 900, SC_KEY, KEY_DDOWN },
                { -1, 900, SC_KEY, KEY_DDOWN }, { -1, 1500, SC_KEY, KEY_B }, { -1, 1500, SC_KEY, KEY_B },
                { APP_MENU_MAIN, 1500, SC_KEY, KEY_X },      /* Halloween theme */
                { APP_MENU_MAIN, 8000, SC_KEY, KEY_X },      /* and back */
                { APP_MENU_MAIN, 2000, SC_DONE, 0 },
            };
            static int sc_i = 0;
            static uint64_t sc_t0 = 0;
            const uint64_t now = osGetTime();
            if (sc_t0 == 0) sc_t0 = now;
            if (sc_i < (int)(sizeof(steps) / sizeof(steps[0]))) {
                const sc_step_t* p = &steps[sc_i];
                if (p->state >= 0 && (int)state != p->state) {
                    sc_t0 = now;                             /* the step's time counts from reaching its screen */
                } else if (now - sc_t0 >= p->ms) {
                    switch (p->op) {
                        case SC_KEY: drive_key = p->arg; break;
                        case SC_SEED:   /* everything open and a stocked save, in memory (the launcher restores the real save) */
                            g_save.settings.unlock_all = 1;
                            g_save.settings.skip_cutscene = 0;
                            bb_give_money(&g_save, 5000);
                            bb_give_exp(&g_save, 2600);
                            for (int i = 0; i < g_extras.shop_count; i += 2) bb_player_add_item(&g_save.player, g_extras.shop[i].item);
                            for (int i = 0; i < g_extras.collect_count; i += 2) bb_player_add_item(&g_save.player, g_extras.collect[i].item);
                            for (int i = 0; i < g_extras.gallery_count; i++) if (g_extras.gallery[i].item) bb_player_add_item(&g_save.player, g_extras.gallery[i].item);
                            bb_ach_unlock(&g_save, BB_ACH_TASTEOFVICTORY);
                            bb_ach_unlock(&g_save, BB_ACH_COMBOKING);
                            bb_ach_unlock(&g_save, BB_ACH_NICE);
                            bb_cs_set_autoplay(true);
                            break;
                        case SC_CURSOR: cursor = (int)p->arg; break;
                        case SC_STAGE:
                            for (int i = 0; i < g_stage_count; i++) {
                                if (strcmp(g_stages[i].id, "cathy_e2") != 0) continue;
                                for (int a = 0; a < g_act_count; a++) if (strcmp(g_act_ids[a], g_stages[i].act) == 0) cur_act = a;
                                int k = 0;
                                for (int j = 0; j < i; j++) if (strcmp(g_stages[j].act, g_stages[i].act) == 0) k++;
                                cursor = k;
                            }
                            break;
                        case SC_DIFF0: diff = 0; break;
                        case SC_LAUNCH:
                            opt_autoplay = true;
                            g_powerup = 0;
                            bb_uisfx_play(BB_UISFX_ACCEPT);
                            want_launch = true;
                            break;
                        case SC_NODE:
                            if (g_scr.node == SCN_GALLERY) bb_gal_close();
                            if (g_scr.node == SCN_COLLECT) bb_coll_close();
                            switch (p->arg) {
                                case 1: case 2: g_scr.node = SCN_SHOP_LIST; g_scr.shop_cat = (int)p->arg - 1; g_scr.cursor[SCN_SHOP_LIST] = 1; g_scr.list_t0 = bb_ui_time(); break;
                                case 3: g_scr.node = SCN_SHOP_TALK; break;
                                case 10: g_scr.node = SCN_GALLERY; bb_gal_open(&g_save); break;
                                case 11: g_scr.node = SCN_CHARS; g_scr.page = 1; break;
                                case 12: g_scr.node = SCN_COLLECT; bb_coll_open(&g_extras, &g_save); break;
                                default: g_scr.node = SCN_THEATER; g_scr.cursor[SCN_THEATER] = 3; break;
                            }
                            bb_ui_enter();
                            break;
                        case SC_HOME:
                            if (g_scr.node == SCN_GALLERY) bb_gal_close();
                            if (g_scr.node == SCN_COLLECT) bb_coll_close();
                            bb_screens_close(&g_scr);
                            bb_uisfx_play(BB_UISFX_BACK);
                            ENTER(APP_MENU_MAIN);
                            cursor = 0;
                            break;
                        default: bb_diag_log("showcase: done"); break;
                    }
                    bb_diag_log("showcase: step %d op %d in %s", sc_i, p->op, app_state_name(state));
                    sc_i++;
                    sc_t0 = now;
                }
            }
        }
        if (smoke_uiperf) {   /* each screen: enter, let the entrance (and its loads) settle for 0.7 s, then measure 2.3 s */
            static const char* const nm[] = { "main", "levels", "difficulty", "mods", "settings", "shop", "shop_list", "extras",
                                              "collection", "gallery", "achievements", "theater" };
            static int ustep = -1;
            static double usum = 0.0, umax = 0.0;
            static uint32_t un = 0, uover12 = 0, uover16 = 0;
            const int nsteps = (int)(sizeof(nm) / sizeof(nm[0]));
            if (ustep >= 0 && state_ms >= 700 && state_ms < 3000) {
                usum += last_busy_ms;
                if (last_busy_ms > umax) umax = last_busy_ms;
                un++;
                if (last_busy_ms > 12.0) uover12++;
                if (last_busy_ms > 16.0) uover16++;
            }
            if ((ustep < 0 && state == APP_MENU_MAIN && state_ms > 300) || (ustep >= 0 && state_ms >= 3000)) {
                if (ustep >= 0)
                    bb_diag_log("UIPERF %s frames=%lu busy_avg=%.2fms busy_max=%.1fms over12ms=%lu over16ms=%lu lin=%luKiB heap=%luKiB", nm[ustep],
                                (unsigned long)un, un ? usum / (double)un : 0.0, umax, (unsigned long)uover12, (unsigned long)uover16,
                                (unsigned long)(linearSpaceFree() / 1024), (unsigned long)bb_hw_heap_free_kib());
                if (ustep < 0) {   /* the same seeded economy as the screens smoke, so shop / extras lists are populated */
                    bb_give_money(&g_save, 5000);
                    bb_give_exp(&g_save, 2600);
                    for (int i = 0; i < g_extras.shop_count; i += 2) bb_player_add_item(&g_save.player, g_extras.shop[i].item);
                    for (int i = 0; i < g_extras.collect_count; i += 3) bb_player_add_item(&g_save.player, g_extras.collect[i].item);
                }
                usum = umax = 0.0;
                un = uover12 = uover16 = 0;
                ustep++;
                if (ustep >= nsteps) { bb_diag_log("SMOKE END uiperf"); goto quit; }
                switch (ustep) {
                    case 0: ENTER(APP_MENU_MAIN); break;
                    case 1: ENTER(APP_MENU_LEVELS); break;
                    case 2: cur_act = 1; cursor = 0; stage = 5; ENTER(APP_MENU_DIFF); break;
                    case 3: ENTER(APP_MENU_MODS); break;
                    case 4: ENTER(APP_MENU_SETTINGS); break;
                    case 5: bb_screens_open(&g_scr, SC_SHOP); ENTER(APP_SCREEN); break;
                    case 6: g_scr.node = SCN_SHOP_LIST; g_scr.shop_cat = 1; g_scr.cursor[SCN_SHOP_LIST] = 1; g_scr.list_t0 = bb_ui_time(); ENTER(APP_SCREEN); break;
                    case 7: bb_screens_open(&g_scr, SC_EXTRAS); ENTER(APP_SCREEN); break;
                    case 8: g_scr.node = SCN_COLLECT; bb_coll_open(&g_extras, &g_save); ENTER(APP_SCREEN); break;
                    case 9: g_scr.node = SCN_GALLERY; bb_gal_open(&g_save); ENTER(APP_SCREEN); break;
                    case 10: bb_screens_open(&g_scr, SC_ACHIEVEMENTS); ENTER(APP_SCREEN); break;
                    default: bb_screens_open(&g_scr, SC_EXTRAS); g_scr.node = SCN_THEATER; g_scr.cursor[SCN_THEATER] = 3; ENTER(APP_SCREEN); break;
                }
            }
        }
        if (smoke_screens && !smoke_uiperf && !smoke_shopflow && !smoke_calib && !smoke_preview && !smoke_egg && !smoke_touch && !smoke_collect) {
            static const char* const nm[] = { "shop_main", "shop_pu", "shop_photos", "shop_music", "shop_outfits", "shop_talk",
                                              "shop_outfit", "extras", "characters", "collection", "gallery", "gallery_view", "achievements", "powerup", "theater" };
            static int step = -1;
            const int nsteps = (int)(sizeof(nm) / sizeof(nm[0]));
            if (step < 0 && state == APP_MENU_MAIN && state_ms > 300) {
                bb_give_money(&g_save, 5000);
                bb_give_exp(&g_save, 2600);
                for (int i = 100; i <= 104; i++) bb_player_add_item(&g_save.player, i);
                for (int i = 0; i < g_extras.shop_count; i += 2) bb_player_add_item(&g_save.player, g_extras.shop[i].item);
                for (int i = 0; i < g_extras.collect_count; i += 3) bb_player_add_item(&g_save.player, g_extras.collect[i].item);
                bb_ach_unlock(&g_save, BB_ACH_TASTEOFVICTORY);
                bb_screens_open(&g_scr, SC_SHOP);
                step = 0;
                ENTER(APP_SCREEN);
            } else if (step >= 0 && state == APP_SCREEN && state_ms > 600) {
                bb_diag_capture(nm[step]);
                step++;
                if (step >= nsteps) { bb_diag_log("SMOKE END screens"); goto quit; }
                switch (step) {
                    case 1: case 2: case 3: case 4: g_scr.node = SCN_SHOP_LIST; g_scr.shop_cat = step - 1; g_scr.cursor[SCN_SHOP_LIST] = 1; g_scr.list_t0 = bb_ui_time(); break;
                    case 5: g_scr.node = SCN_SHOP_TALK; break;
                    case 6: g_scr.node = SCN_SHOP_OUTFIT; g_scr.cursor[SCN_SHOP_OUTFIT] = 1; break;
                    case 7: bb_screens_open(&g_scr, SC_EXTRAS); break;
                    case 8: g_scr.node = SCN_CHARS; g_scr.page = 1; break;
                    case 9: g_scr.node = SCN_COLLECT; bb_coll_open(&g_extras, &g_save); break;
                    case 10: g_scr.node = SCN_GALLERY; bb_gal_open(&g_save); break;
                    case 11: g_scr.node = SCN_GALLERY; break;
                    case 12: bb_screens_open(&g_scr, SC_ACHIEVEMENTS); break;
                    case 13: bb_screens_open(&g_scr, SC_POWERUP); break;
                    default: bb_screens_open(&g_scr, SC_EXTRAS); g_scr.node = SCN_THEATER; g_scr.cursor[SCN_THEATER] = 3; break;
                }
                ENTER(APP_SCREEN);
            }
        }
        if (smoke && state == APP_LOADING && state_ms > 420 && state_ms < 640) {
            static uint64_t ld_t0 = 0;
            if (ld_t0 != state_t0) { ld_t0 = state_t0; bb_diag_capture("loading"); }
        }
        if (smoke && state == APP_RESULTS && state_ms > 2900 && state_ms < 3300) {
            static uint64_t mid_t0 = 0;
            if (mid_t0 != state_t0) { mid_t0 = state_t0; bb_diag_capture("results_mid"); }
        }
        if (smoke && state == APP_RESULTS && state_ms > (uint64_t)BB_RESULTS_ANIM_MS + 1150 && state_ms < (uint64_t)BB_RESULTS_ANIM_MS + 1350) {
            static uint64_t fin_t0 = 0;
            if (fin_t0 != state_t0) { fin_t0 = state_t0; bb_diag_capture("results"); }
        }
        if (smoke_results && state == APP_RESULTS && state_ms > 4200 && state_ms < 4400) {   /* the white flash */
            static uint64_t fl_t0 = 0;
            if (fl_t0 != state_t0) { fl_t0 = state_t0; bb_diag_capture("results_flash"); }
        }
        if (smoke_results && state == APP_RESULTS && state_ms > 5800 && state_ms < 6100) {   /* the first banner held */
            static uint64_t b1_t0 = 0;
            if (b1_t0 != state_t0) { b1_t0 = state_t0; bb_diag_capture("results_banner"); }
        }
        if (smoke_results && state == APP_RESULTS && state_ms > 8100 && state_ms < 8400) {   /* the trophy */
            static uint64_t b2_t0 = 0;
            if (b2_t0 != state_t0) { b2_t0 = state_t0; bb_diag_capture("results_trophy"); }
        }
        if (smoke_results && state == APP_RESULTS && state_ms > 9500 && state_ms < 9700) {   /* both gone again */
            static uint64_t b3_t0 = 0;
            if (b3_t0 != state_t0) { b3_t0 = state_t0; bb_diag_capture("results_clear"); }
        }
        if (smoke_egg) {   /* five taps on the BoobClick area (one off it first), captures, then Options: one tap selects, a second opens */
            static int estep = 0;
            static uint64_t et0 = 0;
            const uint64_t now = osGetTime();
            if (state == APP_MENU_MAIN && estep == 0 && state_ms > 600) { estep = 1; et0 = now; }
            else if (estep >= 1 && estep <= 6 && now - et0 >= 450u * (uint64_t)estep) {
                if (estep == 1) { smoke_tx = 300; smoke_ty = 30; }           /* outside: nothing */
                else { smoke_tx = 160 + 6 * estep; smoke_ty = 110 + 4 * estep; }
                bb_diag_log("egg smoke: tap %d at (%d,%d)", estep, smoke_tx, smoke_ty);
                estep++;
            }
            if (estep == 4 && now - et0 >= 450u * 3u + 200u) { static bool c1 = false; if (!c1) { c1 = true; bb_diag_capture("egg_stars"); } }
            if (estep == 7 && now - et0 >= 450u * 6u + 900u) {
                bb_diag_capture("egg_naked");
                bb_diag_log("egg smoke: loop variant=%d", bb_art_mainmenu_current());
                smoke_tx = 40; smoke_ty = (int)bb_ui_main_item_y(1) + 10;   /* "Options": the first tap selects it */
                estep = 8;
            } else if (estep == 8 && now - et0 >= 450u * 6u + 1500u) {
                bb_diag_log("egg smoke: first tap on Options -> state=%s cursor=%d", app_state_name(state), cursor);
                smoke_tx = 40; smoke_ty = (int)bb_ui_main_item_y(1) + 10;   /* the second tap opens it */
                estep = 9;
            } else if (estep == 9 && state == APP_MENU_SETTINGS) {
                bb_diag_log("SMOKE END egg variant=%d state=%s", bb_art_mainmenu_current(), app_state_name(state));
                goto quit;
            }
        }
        if (smoke_fail && state == APP_FAIL_TAPE && state_ms > 120 && state_ms < 260) {
            static uint64_t rp_t0 = 0;
            if (rp_t0 != state_t0) { rp_t0 = state_t0; bb_diag_capture("fail_ripple"); }
        }
        if (smoke_fail && state == APP_FAIL_TAPE && state_ms > 900 && state_ms < 1100) {
            static uint64_t fr_t0 = 0;
            if (fr_t0 != state_t0) { fr_t0 = state_t0; bb_diag_capture("fail_rise"); }
        }
        if (smoke_fail && state == APP_GAMEOVER && state_ms > 2000 && state_ms < 2400) {
            bb_diag_capture("gameover");
        }
    }

quit:
    if (smoke_perf)
        bb_diag_log("PERF summary frames=%lu over20ms=%lu over34ms=%lu worst=%.1fms at chart=%+.2fs underruns=%lu sheet_loads=%lu busy_avg=%.2fms busy_max=%.1fms new3ds=%d",
                    (unsigned long)perf_frames, (unsigned long)perf_over20, (unsigned long)perf_over34, perf_worst, perf_worst_at,
                    (unsigned long)g_audio.underrun_count, (unsigned long)g_anim.loads, perf_frames ? busy_perf_sum / (double)perf_frames : 0.0,
                    busy_perf_max, bb_hw()->new3ds);
    bb_diag_log("QUIT begin deaths=%lu", (unsigned long)deaths);
    aptUnhook(&g_apt_cookie);
    /* The boot clips can still be live here: Quit on the 18+ warning plays the easter-egg clip and exits as it ends. Its page
     * loader thread (core 2 on New models) and its NDSP channel must be stopped before RomFS, NDSP and the GPU go away, or the
     * console crashes on exit; HOME was disabled for the clip. */
    /* Wait for the GPU to finish the last frame (it may still have sampled the clip's pages) - only when the game owns the GPU.
     * When the HOME Menu closes the suspended game, the HOME Menu holds the GPU and the screens: a frame would never finish and
     * the console hung at "Closing suspended software" (New 2DS XL). Nothing of ours is drawing in that case anyway. */
    if (!aptShouldClose() && gspHasGpuRight()) {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C3D_FrameEnd(0);
    }
    bb_video_shutdown(&g_egg);
    bb_video_shutdown(&g_intro);
    g_egg_on = false;
    g_intro_ok = false;
    bb_boot_free();
    aptSetHomeAllowed(true);
    bb_audio_stop_stream(&g_audio);
    bb_diag_log("QUIT audio stopped");
    unload_stage_assets();
    bb_diag_log("QUIT stage assets freed");
    bb_uisfx_shutdown();
    bb_font_shutdown();
    bb_art_shutdown();
    bb_view_ui_shutdown();
    bb_audio_shutdown(&g_audio);
    bb_diag_log("QUIT audio shut down");
    C2D_TextBufDelete(text_buf);
    C2D_Fini();
    C3D_Fini();
    if (romfs_ok) romfsExit();
    bb_diag_log("QUIT OK");
    bb_diag_shutdown();
    gfxExit();
    return 0;
}
