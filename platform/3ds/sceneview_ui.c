/**
 * @file sceneview_ui.c
 * @brief Extras > Scene Viewer (see sceneview_ui.h).
 */
#include "sceneview_ui.h"
#include "menu_ui.h"
#include "game_view.h"
#include "ui_motion.h"
#include "ui_sfx.h"
#include "ui_art.h"
#include "font.h"
#include "extras.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define ZP 0.35f
#define ZI 0.45f
#define ZT 0.50f
#define N_LV 8
#define MAX_ANIM 64

/* scene_viewer.gd bumper_scenes / scene_viewer_song_selector.gd SONGS, in order: stage, title, song bpm, level item (Save.unlock_level) */
static const struct { const char* id; const char* title; const char* who; int bpm; int item; } k_lv[N_LV] = {
    { "cathy_e1", "Virgin Newbie", "Cathy", 126, 501 }, { "zoe_e1", "She Doesnt Like You", "Zoe", 128, 502 },
    { "gale_e1", "I Can Do It Better", "Gale", 130, 503 }, { "claire_e1", "The Boss", "Claire", 128, 504 },
    { "dawna_e1", "Secret Weapon", "Dawna", 128, 505 }, { "zoe_e2", "In The Closet", "Zoe", 140, 506 },
    { "cathy_e2", "In The Dorms", "Cathy", 140, 507 }, { "makayla", "After Class", "Makayla", 128, 598 },
};

typedef struct { int type; int index; int64_t ts; } sv_anim_t;   /* type 0 loop (anim sheet), 1 video */

static struct {
    bool open;
    bb_save_t* save;
    int level;              /* selected level (k_lv) */
    int loaded;             /* level whose assets main.c loaded, -1 none */
    int want;               /* level to load, -1 none */
    int shown_loading;      /* frames the loading screen has been drawn */
    const bb_chart_t* chart;
    bb_anim_t* anim;
    bb_video_t* videos;
    int video_count;
    sv_anim_t list[MAX_ANIM];
    int n, cur;             /* timeline_proxy.loaded_animations / animation_index */
    int sheet;              /* the sheet drawn this beat (variation picked per loop) */
    uint32_t frame;
    int song;               /* -1 No Song, else k_lv index */
    char song_path[64];
    int bpm;                /* custom clock bpm (SpinBox 30-300, 120) */
    bool clock_on;          /* custom_clock_active */
    double clock_s;         /* custom clock time */
    int64_t music_us;
    int beat, last_beat;
    float beat_t;           /* last beat (frame tween start) */
    float pump_t;           /* character_root.pulse */
    float video_t0;
    bool movie;             /* movie mode: 1.5x */
    float zoom;             /* smoothed 1.0 / 1.5 */
    uint32_t rng;
    float last_t;
    int focus;              /* bottom rows for the D-pad: 0 levels, 1 animations, 2 song, 3 bpm */
    C2D_SpriteSheet thumbs;
    C2D_SpriteSheet web;    /* sv_web.t3x: site logo, hamburger, fullscreen icon */
    float scroll, content_h;   /* the bottom screen scrolls vertically (drag / C-stick) */
    bool  down, dragged, ptap; /* touch: buttons act on release unless the stylus dragged */
    int   dx0, dy0, ptx, pty;
    float s0;
} V = { .loaded = -1, .want = -1, .song = -1, .bpm = 120, .zoom = 1.0f, .rng = 0x51A7E5u };

static u32 rgba(u8 r, u8 g, u8 b, float a) { return C2D_Color32(r, g, b, (u8)(a < 0.0f ? 0.0f : (a > 1.0f ? 255.0f : a * 255.0f))); }
static bool lv_open(int k) {
    return V.save && (V.save->settings.unlock_all || bb_player_has_item(&V.save->player, k_lv[k].item));
}
static int open_count(void) { int n = 0; for (int k = 0; k < N_LV; k++) if (lv_open(k)) n++; return n; }

void bb_sv_open(bb_save_t* save) {
    bb_sv_close();
    V.open = true;
    V.save = save;
    V.song = -1;          /* song_label "No Song" */
    V.bpm = 120;
    V.clock_on = true;    /* custom_bpm._ready: start_custom_clock() */
    V.clock_s = 0.0;
    V.movie = false;
    V.zoom = 1.0f;
    V.n = V.cur = 0;
    V.sheet = -1;
    V.last_t = bb_ui_time();
    V.focus = 1;
    V.thumbs = bb_extras_load_sheet("sv_thumbs.t3x");
    V.web = C2D_SpriteSheetLoad("romfs:/extras/sv_web.t3x");
    V.scroll = 0.0f;
    if (V.thumbs) C3D_TexSetFilter(C2D_SpriteSheetGetImage(V.thumbs, 0).tex, GPU_LINEAR, GPU_LINEAR);
    V.level = -1;
    for (int k = 0; k < N_LV && V.level < 0; k++) if (lv_open(k)) V.level = k;   /* cathy_v_1._on_button_up(): the first open level */
    V.want = V.level;
    V.shown_loading = 0;
}

static void stop_video(void) {
    if (V.n > 0 && V.cur < V.n && V.list[V.cur].type == 1 && V.videos) bb_video_reset(&V.videos[V.list[V.cur].index]);
}

void bb_sv_close(void) {
    if (V.open) stop_video();
    if (V.thumbs) { C2D_SpriteSheetFree(V.thumbs); V.thumbs = NULL; }
    if (V.web) { C2D_SpriteSheetFree(V.web); V.web = NULL; }
    V.open = false;
    V.chart = NULL; V.anim = NULL; V.videos = NULL; V.video_count = 0;
    V.loaded = -1;
}

const char* bb_sv_want_stage(void) {
    if (!V.open || V.want < 0 || V.shown_loading < 1) return NULL;
    return k_lv[V.want].id;
}

static void pick_variation(void) {   /* Config.pick_variation_index: roll = rand * max(100, total weight), first bucket wins */
    const sv_anim_t* e = &V.list[V.cur];
    V.sheet = e->index;
    if (!V.anim || e->index < 0) return;
    const bb_anim_sheet_t* sh = &V.anim->sheet[e->index];
    float total = 0.0f;
    for (int k = 0; k < sh->var_count; k++) total += sh->var_weight[k];
    V.rng = V.rng * 1664525u + 1013904223u;
    float roll = (float)(V.rng >> 8) / 16777216.0f * (total > 100.0f ? total : 100.0f);
    for (int k = 0; k < sh->var_count; k++) {
        roll -= sh->var_weight[k];
        if (roll < 0.0f) { V.sheet = sh->var_idx[k]; return; }
    }
}

static void set_animation(int i, bool sound) {   /* timeline_proxy.change_animation */
    if (V.n <= 0) return;
    if (i < 0) i = 0;
    if (i >= V.n) i = V.n - 1;
    stop_video();
    V.cur = i;
    if (V.list[i].type == 0) {
        bb_view_sv_prepare(V.anim, V.list[i].index);
        pick_variation();
        V.frame = 0;
    } else {
        bb_video_reset(&V.videos[V.list[i].index]);
        V.video_t0 = bb_ui_time();
    }
    if (sound) bb_uisfx_play(BB_UISFX_CLICK);   /* ui_mouse_click_2 */
    V.pump_t = bb_ui_time();
}

void bb_sv_stage_loaded(const bb_chart_t* chart, bb_anim_t* anim, bb_video_t* videos, int video_count) {
    V.chart = chart; V.anim = anim; V.videos = videos; V.video_count = video_count;
    V.loaded = V.want;
    V.want = -1;
    V.n = 0;
    /* setup_animations (every loop keyframe) + setup_videos, sorted by timestamp */
    for (uint32_t i = 0; chart && i < chart->loop_count && V.n < MAX_ANIM; i++) {
        const int s = anim ? bb_anim_find(anim, chart->loops[i].name) : -1;
        if (s < 0) continue;
        V.list[V.n++] = (sv_anim_t){ 0, s, chart->loops[i].timestamp_us };
    }
    for (int i = 0; i < video_count && V.n < MAX_ANIM; i++) V.list[V.n++] = (sv_anim_t){ 1, i, videos[i].start_us };
    for (int a = 1; a < V.n; a++) {   /* stable insertion sort by timestamp */
        sv_anim_t t = V.list[a];
        int b = a - 1;
        while (b >= 0 && V.list[b].ts > t.ts) { V.list[b + 1] = V.list[b]; b--; }
        V.list[b + 1] = t;
    }
    bb_uisfx_play(BB_UISFX_CLICK);   /* load_set_level: ui_mouse_click */
    set_animation(0, false);
}

const char* bb_sv_want_music(void) {
    if (!V.open || V.song < 0) return NULL;
    snprintf(V.song_path, sizeof(V.song_path), "romfs:/stages/%s/audio/%s.wav", k_lv[V.song].id, k_lv[V.song].id);
    return V.song_path;
}

void bb_sv_set_music_pos(int64_t pos_us) { V.music_us = pos_us; }

static void choose_level(int k, bool sound) {   /* scene_viewer_level_button -> EventManager.scene_viewer_change_level */
    if (k < 0 || k >= N_LV) return;
    if (!lv_open(k)) { bb_uisfx_play(BB_UISFX_LOCK); return; }
    if (k == V.level && V.loaded == k) return;
    if (sound) bb_uisfx_play(BB_UISFX_TICK);
    stop_video();
    V.level = k;
    V.want = k;
    V.shown_loading = 0;
    V.n = 0;
}

static void bumper(int dir) {   /* bumper_press: the next open level, wrapping */
    for (int s = 1; s <= N_LV; s++) {
        const int k = ((V.level + dir * s) % N_LV + N_LV) % N_LV;
        if (lv_open(k)) { choose_level(k, true); return; }
    }
}

static void cycle_song(int dir) {   /* scene_viewer_song_selector.cycle_song: -1 = No Song, then the songs of beaten levels */
    int idx = V.song;
    for (int s = 0; s < N_LV + 1; s++) {
        idx += dir;
        if (idx >= N_LV) idx = -1;
        if (idx < -1) idx = N_LV - 1;
        if (idx == -1 || lv_open(idx)) break;
    }
    V.song = idx;
    V.clock_on = idx < 0 ? V.clock_on : false;   /* a song stops the custom clock */
    if (idx < 0) { V.clock_on = true; V.clock_s = 0.0; }
    V.music_us = 0;
    V.last_beat = -1;
    bb_uisfx_play(BB_UISFX_TICK);
}

void bb_sv_touch(bool held, bool tap, int x, int y) {
    if (!V.open) return;
    if (tap && !held) { V.ptap = true; V.ptx = x; V.pty = y; V.down = false; return; }   /* a tap without a hold (smoke injection) */
    if (held) {
        if (!V.down) { V.down = true; V.dragged = false; V.dx0 = x; V.dy0 = y; V.s0 = V.scroll; }
        const int dy = y - V.dy0;
        if (dy > 6 || dy < -6) V.dragged = true;
        if (V.dragged) V.scroll = V.s0 - (float)dy;
    } else if (V.down) {
        V.down = false;
        if (!V.dragged) { V.ptap = true; V.ptx = V.dx0; V.pty = V.dy0; }
    }
}

int bb_sv_update(u32 kDown, u32 kRep, bool tap, int tx, int ty) {
    if (!V.open) return 1;
    const float now = bb_ui_time();
    const float dt = now - V.last_t;
    V.last_t = now;
    if (V.ptap) { tap = true; tx = V.ptx; ty = V.pty; V.ptap = false; }
    if (kRep & (KEY_CSTICK_DOWN | KEY_CPAD_DOWN)) V.scroll += 18.0f;   /* the circle pad and the C-stick scroll the controls */
    if (kRep & (KEY_CSTICK_UP | KEY_CPAD_UP)) V.scroll -= 18.0f;
    {
        const float maxs = V.content_h - (240.0f - 30.0f);
        if (V.scroll > maxs) V.scroll = maxs;
        if (V.scroll < 0.0f) V.scroll = 0.0f;
    }
    if (tap) {
        const int z = bb_ui_zone_hit(tx, ty);
        if (z == BB_ZONE_BACK) kDown |= KEY_B;
        else if (z == BB_ZONE_PREV) kDown |= KEY_DLEFT;
        else if (z == BB_ZONE_NEXT) kDown |= KEY_DRIGHT;
        else if (z >= BB_ZONE_LEVEL && z < BB_ZONE_LEVEL + N_LV) choose_level(z - BB_ZONE_LEVEL, true);
        else if (z >= BB_ZONE_ACT && z < BB_ZONE_ACT + MAX_ANIM) set_animation(z - BB_ZONE_ACT, true);
        else if (z == BB_ZONE_PAUSE + 0) cycle_song(-1);
        else if (z == BB_ZONE_PAUSE + 1) cycle_song(1);
        else if (z == BB_ZONE_PAUSE + 2) { V.bpm = V.bpm - 5 < 30 ? 30 : V.bpm - 5; bb_uisfx_play(BB_UISFX_TOUCH); }
        else if (z == BB_ZONE_PAUSE + 3) { V.bpm = V.bpm + 5 > 300 ? 300 : V.bpm + 5; bb_uisfx_play(BB_UISFX_TOUCH); }
        else if (z == BB_ZONE_PAUSE + 4) kDown |= KEY_A;
        else if (z == BB_ZONE_PAUSE + 5) kDown |= KEY_X;
    }
    if (kDown & KEY_B) {   /* leave_scene_viewer: movie mode first */
        if (V.movie) { kDown |= KEY_X; }
        else { bb_uisfx_play(BB_UISFX_BACK); return 1; }
    }
    if (V.loaded >= 0 && V.n > 0) {
        if (kRep & (KEY_DLEFT | KEY_CPAD_LEFT)) set_animation(V.cur - 1, true);
        if (kRep & (KEY_DRIGHT | KEY_CPAD_RIGHT)) set_animation(V.cur + 1, true);
    }
    if (kDown & KEY_L) bumper(-1);
    if (kDown & KEY_R) bumper(1);
    if (kDown & KEY_Y) cycle_song(1);
    if (kRep & KEY_DUP) V.bpm = V.bpm + 5 > 300 ? 300 : V.bpm + 5;     /* the D-pad's up / down keep the tempo */
    if (kRep & KEY_DDOWN) V.bpm = V.bpm - 5 < 30 ? 30 : V.bpm - 5;
    if (kDown & KEY_A) {   /* custom_bpm Play: toggle / restart the clock (also stops the song) */
        if (V.song >= 0) { V.song = -1; V.clock_on = true; V.clock_s = 0.0; }
        else V.clock_on = !V.clock_on;
        bb_uisfx_play(BB_UISFX_CLICK);
    }
    if (kDown & KEY_X) {   /* toggle_scene_viewer_fullscreen: 1.5x, 0.2 s expo */
        V.movie = !V.movie;
        bb_uisfx_play(V.movie ? BB_UISFX_GALLERY_ZOOM_IN : BB_UISFX_GALLERY_ZOOM_OUT);
    }
    /* the clock: the song's position (its bpm) or the custom clock */
    const int bpm = V.song >= 0 ? k_lv[V.song].bpm : V.bpm;
    if (V.song < 0 && V.clock_on) V.clock_s += dt > 0.1f ? 0.1f : dt;
    const double t = V.song >= 0 ? (double)V.music_us / 1e6 : V.clock_s;
    const int beat = (int)(t * (double)bpm / 60.0);
    if (beat != V.last_beat && (V.song >= 0 || V.clock_on)) {   /* scene_viewer_beat -> run_loop */
        V.last_beat = beat;
        V.beat_t = now;
        if (V.n > 0 && V.list[V.cur].type == 0) pick_variation();
    }
    if (V.n > 0 && V.list[V.cur].type == 0 && V.anim && V.sheet >= 0) {   /* frame 0 -> total-1 over one beat */
        const uint32_t total = V.anim->sheet[V.sheet].frame_count;
        const float f = (now - V.beat_t) * (float)bpm / 60.0f;
        V.frame = total > 1 ? (uint32_t)((f > 1.0f ? 1.0f : f) * (float)(total - 1)) : 0;
    }
    if (V.n > 0 && V.list[V.cur].type == 1) {
        bb_video_t* vv = &V.videos[V.list[V.cur].index];
        bb_video_update(vv, vv->start_us + (int64_t)((now - V.video_t0) * 1e6f));
    }
    bb_ui_smooth(&V.zoom, 1.0f, 18.0f);   /* fullscreen = the whole top screen at 1x, the page frame hidden (no zoom) */
    return 0;
}

/* ---------------------------------------------------------------- top: the scene */
/* scene_viewer.tscn WebUi at 1080p, mapped to the top screen (K = 240/1080, 13 px of each side cropped): the header bar (#0E0E0E, 137 px)
 * with the site logo at (320, 72), the search bar 528..1456 x 40..104 and the hamburger at (1840, 72); the title at y 920, the view
 * count at y 984 and the bottom line at y 1032. The scene plays in the space between, like the page's video player. */
#define WK (240.0f / 1080.0f)
#define WX(x) ((x) * WK - 13.3f)
static void web_cell(float sx, float sw, float sh, float cx, float cy, float w, float h, float z) {
    if (!V.web) return;
    C2D_Image img = C2D_SpriteSheetGetImage(V.web, 0);
    Tex3DS_SubTexture st = { (u16)sw, (u16)sh, sx / 512.0f, 1.0f, (sx + sw) / 512.0f, 1.0f - sh / 64.0f };
    img.subtex = &st;
    C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
    C2D_DrawParams dp = { { cx - w * 0.5f, cy - h * 0.5f, w, h }, { 0, 0 }, z, 0.0f };
    C2D_DrawImage(img, &dp, NULL);
}

static void web_frame(void) {
    const float zf = 0.90f;
    C2D_DrawRectSolid(0, 0, zf, 400, 137.0f * WK, C2D_Color32(0x0E, 0x0E, 0x0E, 0xFF));
    web_cell(0.0f, 218.0f, 64.0f, WX(320.0f) + 6.0f, 72.0f * WK, 218.0f * WK * 1.25f, 64.0f * WK * 1.25f, zf + 0.01f);
    const float sx0 = WX(528.0f), sx1 = WX(1456.0f), sy0 = 40.0f * WK, sy1 = 104.0f * WK;
    C2D_DrawRectSolid(sx0 + 3.0f, sy0, zf + 0.01f, sx1 - sx0 - 6.0f, sy1 - sy0, C2D_Color32(0x26, 0x26, 0x2A, 0xFF));
    C2D_DrawCircleSolid(sx0 + 3.0f, (sy0 + sy1) * 0.5f, zf + 0.01f, (sy1 - sy0) * 0.5f, C2D_Color32(0x26, 0x26, 0x2A, 0xFF));
    C2D_DrawCircleSolid(sx1 - 3.0f, (sy0 + sy1) * 0.5f, zf + 0.01f, (sy1 - sy0) * 0.5f, C2D_Color32(0x26, 0x26, 0x2A, 0xFF));
    C2D_DrawCircleSolid(sx0 + 9.0f, (sy0 + sy1) * 0.5f - 1.0f, zf + 0.02f, 3.5f, C2D_Color32(0x90, 0x90, 0x98, 0xFF));   /* magnifier */
    C2D_DrawCircleSolid(sx0 + 9.0f, (sy0 + sy1) * 0.5f - 1.0f, zf + 0.021f, 2.2f, C2D_Color32(0x26, 0x26, 0x2A, 0xFF));
    C2D_DrawLine(sx0 + 11.0f, (sy0 + sy1) * 0.5f + 1.0f, C2D_Color32(0x90, 0x90, 0x98, 0xFF), sx0 + 14.0f, (sy0 + sy1) * 0.5f + 4.0f,
                 C2D_Color32(0x90, 0x90, 0x98, 0xFF), 1.5f, zf + 0.021f);
    if (bb_font_ready()) bb_font_draw(BB_FACE_BODY, 10, sx0 + 18.0f, sy0 + 0.5f, C2D_Color32(0x90, 0x90, 0x98, 0xFF), C2D_AlignLeft, "Search BunHub...", zf + 0.02f);
    web_cell(224.0f, 64.0f, 64.0f, 389.0f, 72.0f * WK, 64.0f * 0.7f * WK * 1.6f, 64.0f * 0.7f * WK * 1.6f, zf + 0.01f);
    /* under the player: the title, the views, the line */
    C2D_DrawRectSolid(0, 196.0f, zf, 400, 44.0f, C2D_Color32(0, 0, 0, 0xFF));
    if (bb_font_ready() && V.level >= 0) {
        char t[64];
        snprintf(t, sizeof(t), "%s - %s", k_lv[V.level].who, k_lv[V.level].title);
        bb_font_draw(BB_FACE_BOLD, 14, WX(96.0f), 198.0f, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), C2D_AlignLeft, t, zf + 0.02f);
        /* each scene has its own count (the original adds the level's scores up; here every character starts from her own
         * figure and the player's scores add on top) */
        static const long k_base_views[N_LV] = { 1284000, 962000, 731000, 1517000, 845000, 1103000, 1376000, 2241000 };
        long views = k_base_views[V.level];
        const bb_stage_save_t* sv = V.save ? bb_save_find_stage(V.save, k_lv[V.level].id) : NULL;
        for (int d = 0; sv && d < 3; d++) views += sv->best_score[d];
        if (views >= 1000000) snprintf(t, sizeof(t), "%.1fM Views", (double)views / 1000000.0);
        else if (views >= 1000) snprintf(t, sizeof(t), "%.1fK Views", (double)views / 1000.0);
        else snprintf(t, sizeof(t), "%ld Views", views);
        bb_font_draw(BB_FACE_BODY, 10, WX(96.0f), 216.0f, C2D_Color32(0xA0, 0xA0, 0xA8, 0xFF), C2D_AlignLeft, t, zf + 0.02f);
    }
    C2D_DrawRectSolid(WX(96.0f), 1032.0f * WK, zf + 0.02f, WX(1360.0f) - WX(96.0f), 1.0f, C2D_Color32(0x50, 0x50, 0x58, 0xFF));
}

void bb_sv_draw_top(void) {
    const float now = bb_ui_time();
    if (V.want >= 0 || V.loaded < 0 || V.level < 0) {   /* Loading / NoLevels */
        bb_view_draw_checker(0x40);
        if (bb_font_ready()) {
            if (V.level < 0) {
                bb_font_draw(BB_FACE_HEAD, 30, 200.0f, 84.0f, rgba(0xFF, 0xFF, 0xFF, 1.0f), C2D_AlignCenter, "NO SCENES YET", ZT);
                bb_font_draw(BB_FACE_BOLD, 14, 200.0f, 130.0f, rgba(0xC8, 0xC8, 0xD2, 1.0f), C2D_AlignCenter, "Beat a level to watch it here.", ZT);
            } else {
                bb_font_draw(BB_FACE_HEAD, 30, 200.0f, 96.0f, rgba(0xFF, 0xFF, 0xFF, 1.0f), C2D_AlignCenter, "LOADING", ZT);
            }
        }
        if (V.want >= 0) V.shown_loading++;
        web_frame();
        return;
    }
    /* the player: the whole scene scaled into the page between the header and the title (movie mode: full screen, 1.5x) */
    const float framed = V.movie ? 0.0f : 1.0f;
    const float ps = 1.0f - framed * (1.0f - (196.0f - 30.0f) / 240.0f);
    C2D_DrawRectSolid(0, 0, 0.02f, 400, 240, C2D_Color32(0, 0, 0, 0xFF));
    C2D_ViewReset();
    C2D_ViewTranslate(200.0f - 200.0f * ps, (framed ? 113.0f : 120.0f) - 120.0f * ps);
    C2D_ViewScale(ps, ps);
    if (V.n > 0 && V.list[V.cur].type == 1) {   /* a climax video, held on its last frame when it ends */
        bb_video_t* vv = &V.videos[V.list[V.cur].index];
        int64_t t = vv->start_us + (int64_t)((now - V.video_t0) * 1e6f);
        const int64_t end = vv->start_us + (int64_t)((double)vv->frames * 1e6 / (double)(vv->fps > 0.0f ? vv->fps : 30.0f)) - 1000;
        if (t > end) t = end;
        if (t >= vv->hide_us) t = vv->hide_us - 1;
        bb_video_draw(vv, t, 0.40f);
    } else {
        const float pe = (now - V.pump_t) * (float)(V.song >= 0 ? k_lv[V.song].bpm : V.bpm) / 60.0f;   /* pulse: 1.1 -> 1.0 over a beat */
        const float pump = pe < 1.0f ? 1.1f - 0.1f * bb_ui_ease_out(pe) : 1.0f;
        bb_view_sv_draw(V.anim, V.sheet, V.frame, V.zoom * pump, (int64_t)(now * 1e6f));
    }
    C2D_ViewReset();
    if (!V.movie) {
        web_frame();
        if (bb_font_ready()) {   /* AnimationCount */
            char b[16];
            snprintf(b, sizeof(b), "%d / %d", V.cur + 1, V.n);
            bb_font_draw(BB_FACE_BOLD, 12, 392.0f, 200.0f, rgba(0xFF, 0xFF, 0xFF, 0.85f), C2D_AlignRight, b, 0.95f);
        }
    }
}

/* ---------------------------------------------------------------- bottom: the controls */
static void button(float x, float y, float w, float h, const char* label, bool lit, bool gold, int zone, float a) {
    const u32 edge = lit ? rgba(0xFF, 0xFF, 0xFF, a) : (gold ? rgba(0xCC, 0x9A, 0x00, a) : rgba(0x5A, 0x5A, 0x66, a));
    C2D_DrawRectSolid(x, y, ZP, w, h, edge);
    C2D_DrawRectSolid(x + 1.0f, y + 1.0f, ZP + 0.01f, w - 2.0f, h - 2.0f, lit ? (gold ? rgba(0x6A, 0x50, 0x00, a) : rgba(0x30, 0x30, 0x3C, a)) : rgba(0x0C, 0x0C, 0x12, a));
    if (bb_font_ready()) bb_font_draw(BB_FACE_BOLD, 12, x + w * 0.5f, y + (h - 16.0f) * 0.5f, gold ? rgba(0xFF, 0xE0, 0x40, a) : rgba(0xF0, 0xF0, 0xF6, a), C2D_AlignCenter, label, ZT);
    if (zone >= 0) bb_ui_zone_add(zone, x, y, w, h);
}

static float section(const char* label, const char* right, float y, float a) {
    if (bb_font_ready()) {
        bb_font_draw_smallcaps(BB_FACE_BOLD, 14, 14.0f, y, rgba(0xFF, 0x8A, 0x1E, a), C2D_AlignLeft, label, ZT);
        if (right) bb_font_draw(BB_FACE_BODY, 10, 306.0f, y + 3.0f, rgba(0x9A, 0x9A, 0xA6, a), C2D_AlignRight, right, ZT);
    }
    C2D_DrawRectSolid(14.0f, y + 18.0f, ZP, 292.0f, 1.0f, rgba(0xFF, 0xFF, 0xFF, 0.18f * a));
    return y + 24.0f;
}

/* zones only where the content is visible under the fixed header */
static void vis_zone(int id, float x, float y, float w, float h) {
    if (y + h <= 30.0f || y >= 240.0f) return;
    if (y < 30.0f) { h -= 30.0f - y; y = 30.0f; }
    bb_ui_zone_add(id, x, y, w, h);
}

static void sbutton(float x, float y, float w, float h, const char* label, bool lit, bool gold, int zone, float a) {
    if (y + h < 26.0f || y > 244.0f) return;
    button(x, y, w, h, label, lit, gold, -1, a);
    if (zone >= 0) vis_zone(zone, x, y, w, h);
}

void bb_sv_draw_bottom(void) {
    bb_view_draw_checker(0x60);
    const float a = bb_ui_appear(0.0f, 0.3f);
    float y = 36.0f - V.scroll;
    /* LEVELS: the eight levels, two rows of four with their names */
    y = section("Levels", V.level >= 0 ? k_lv[V.level].title : NULL, y, a);
    for (int k = 0; k < N_LV; k++) {
        const float s = 56.0f, x = 17.0f + (float)(k % 4) * 74.0f, ty = y + (float)(k / 4) * 74.0f;
        if (ty + s < 26.0f || ty > 244.0f) continue;
        const bool on = k == V.level, ok = lv_open(k);
        if (on) C2D_DrawRectSolid(x - 2.0f, ty - 2.0f, ZP, s + 4.0f, s + 4.0f, rgba(0xFF, 0xFF, 0xFF, a));
        if (V.thumbs) {
            C2D_Image img = C2D_SpriteSheetGetImage(V.thumbs, 0);
            Tex3DS_SubTexture st = { 96, 96, (float)((k % 5) * 96) / 512.0f, 1.0f - (float)((k / 5) * 96) / 256.0f,
                                     (float)((k % 5) * 96 + 96) / 512.0f, 1.0f - (float)((k / 5) * 96 + 96) / 256.0f };
            img.subtex = &st;
            C2D_ImageTint t;
            const u8 v = ok ? (on ? 255 : 150) : 40;
            C2D_PlainImageTint(&t, C2D_Color32(v, v, v, (u8)(255.0f * a)), 1.0f);
            C2D_SetTintMode(C2D_TintMult);
            C2D_DrawParams dp = { { x, ty, s, s }, { 0, 0 }, ZI, 0.0f };
            C2D_DrawImage(img, &dp, &t);
        }
        if (!ok) {
            C2D_DrawRectSolid(x + s * 0.5f - 5.0f, ty + s * 0.5f - 1.0f, ZI + 0.02f, 10.0f, 8.0f, rgba(0xC8, 0xC8, 0xD2, a));
            C2D_DrawCircleSolid(x + s * 0.5f, ty + s * 0.5f - 3.0f, ZI + 0.01f, 4.0f, rgba(0xC8, 0xC8, 0xD2, a));
            C2D_DrawCircleSolid(x + s * 0.5f, ty + s * 0.5f - 3.0f, ZI + 0.015f, 2.2f, rgba(0, 0, 0, a));
        }
        if (bb_font_ready()) bb_font_draw(BB_FACE_BODY, 10, x + s * 0.5f, ty + s + 1.0f, on ? rgba(0xFF, 0xFF, 0xFF, a) : rgba(0x9A, 0x9A, 0xA6, a), C2D_AlignCenter, k_lv[k].who, ZT);
        vis_zone(BB_ZONE_LEVEL + k, x - 2.0f, ty - 2.0f, s + 4.0f, s + 4.0f);
    }
    y += 2.0f * 74.0f + 4.0f;
    if (V.level < 0) {
        if (bb_font_ready()) bb_font_draw(BB_FACE_BOLD, 12, 160.0f, y, rgba(0xC8, 0xC8, 0xD2, a), C2D_AlignCenter, "Beat a level to unlock its scene.", ZT);
        y += 24.0f;
    } else {
        /* SCENES: every loop and climax video in timeline order, wrapping nine to a row (videos gold) */
        y = section("Scenes", V.n > 0 && V.list[V.cur].type == 1 ? "climax video" : "loop", y, a);
        for (int i = 0; i < V.n; i++) {
            char b[12];
            snprintf(b, sizeof(b), "%d", i + 1);
            sbutton(16.0f + (float)(i % 9) * 32.0f, y + (float)(i / 9) * 30.0f, 28.0f, 24.0f, b, i == V.cur, V.list[i].type == 1, BB_ZONE_ACT + i, a);
        }
        y += (float)((V.n + 8) / 9) * 30.0f + 4.0f;
        /* SONG */
        y = section("Song", "Y", y, a);
        sbutton(16.0f, y, 32.0f, 24.0f, "<", false, false, BB_ZONE_PAUSE + 0, a);
        sbutton(272.0f, y, 32.0f, 24.0f, ">", false, false, BB_ZONE_PAUSE + 1, a);
        if (bb_font_ready() && y > 20.0f && y < 244.0f)
            bb_font_draw(BB_FACE_BOLD, 14, 160.0f, y + 3.0f, V.song >= 0 ? rgba(0xFF, 0xC8, 0x3C, a) : rgba(0xF0, 0xF0, 0xF6, a), C2D_AlignCenter,
                         V.song >= 0 ? k_lv[V.song].title : "No Song", ZT);
        y += 34.0f;
        /* TEMPO: the custom clock */
        y = section("Tempo", "D-Pad Up / Down", y, a);
        sbutton(16.0f, y, 32.0f, 24.0f, "-", false, false, BB_ZONE_PAUSE + 2, a);
        char b[16];
        snprintf(b, sizeof(b), "%d BPM", V.song >= 0 ? k_lv[V.song].bpm : V.bpm);
        if (bb_font_ready() && y > 20.0f && y < 244.0f) bb_font_draw(BB_FACE_BOLD, 14, 92.0f, y + 3.0f, rgba(0xFF, 0xFF, 0xFF, a), C2D_AlignCenter, b, ZT);
        sbutton(136.0f, y, 32.0f, 24.0f, "+", false, false, BB_ZONE_PAUSE + 3, a);
        sbutton(184.0f, y, 120.0f, 24.0f, (V.song < 0 && V.clock_on) ? "A Pause" : "A Play", V.song < 0 && V.clock_on, false, BB_ZONE_PAUSE + 4, a);
        y += 34.0f;
        if (bb_font_ready() && y > 20.0f && y < 244.0f)
            bb_font_draw(BB_FACE_BODY, 10, 160.0f, y, rgba(0x9A, 0x9A, 0xA6, a), C2D_AlignCenter, open_count() > 1 ? "L / R: level   < >: scene   drag / circle pad: scroll" : "< >: scene   drag / circle pad: scroll", ZT);
        y += 18.0f;
    }
    V.content_h = y + V.scroll - 30.0f;
    /* scroll bar */
    const float view = 240.0f - 30.0f;
    if (V.content_h > view) {
        const float th = view * view / V.content_h, ty = 30.0f + (view - th) * V.scroll / (V.content_h - view);
        C2D_DrawRectSolid(316.0f, ty, ZT + 0.1f, 2.0f, th, rgba(0xFF, 0xFF, 0xFF, 0.45f));
    }
    /* the fixed header: back, the title, fullscreen (the original's FullscreenButton) */
    C2D_DrawRectSolid(0, 0, 0.80f, 320, 28.0f, rgba(0x0E, 0x0E, 0x0E, 0.97f));
    C2D_DrawRectangle(0, 28.0f, 0.80f, 320, 5.0f, rgba(0, 0, 0, 0.6f), rgba(0, 0, 0, 0.6f), rgba(0, 0, 0, 0.0f), rgba(0, 0, 0, 0.0f));
    bb_art_icon(7, 16.0f, 14.0f, 22.0f, C2D_Color32(255, 255, 255, (u8)(255.0f * a)), 0.81f);
    bb_ui_zone_add(BB_ZONE_BACK, 0, 0, 34.0f, 28.0f);
    if (bb_font_ready()) bb_font_draw_smallcaps(BB_FACE_BOLD, 17, 34.0f, 3.0f, rgba(0xFF, 0xFF, 0xFF, a), C2D_AlignLeft, "Scene Viewer", 0.82f);
    if (V.level >= 0) {
        if (V.movie) C2D_DrawRectSolid(286.0f, 3.0f, 0.81f, 26.0f, 22.0f, rgba(0xFF, 0x8A, 0x1E, 0.85f));
        web_cell(296.0f, 64.0f, 64.0f, 299.0f, 14.0f, 20.0f, 20.0f, 0.82f);
        if (bb_font_ready()) bb_font_draw(BB_FACE_BODY, 10, 280.0f, 8.0f, rgba(0x9A, 0x9A, 0xA6, a), C2D_AlignRight, "X", 0.82f);
        bb_ui_zone_add(BB_ZONE_PAUSE + 5, 270.0f, 0, 50.0f, 28.0f);
    }
}
