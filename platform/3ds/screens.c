/**
 * @file screens.c
 * @brief Shop, Extras, Achievements and power-up selection screens (see screens.h).
 *
 * Presentation follows the reference footage (menu tour): shop rows with orange names / gold prices / SOLD OUT stamp and a
 * speech bubble with typewriter text; Extras with the torn black side panel, icons and the slanted EXTRAS banner; the
 * achievement tile grid with tooltip; a thumbnail gallery grid; character bios with Origins / Residence / Hobbies; and the
 * collection notebook. All drawing is top screen only.
 */
#include "screens.h"
#include "theme.h"
#include "collection_ui.h"
#include "gallery_ui.h"
#include "music_ui.h"
#include "sceneview_ui.h"
#include "game_view.h"
#include "progress.h"
#include "ui_art.h"
#include "ui_motion.h"
#include "ui_sfx.h"
#include "menu_ui.h"
#include "font.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define ZP 0.35f   /* panels (above the dimmed background at 0.30) */
#define ZI 0.45f   /* images */
#define ZT 0.50f   /* text */

static C2D_TextBuf g_tb;

static const char* const k_cat_name[4] = { "Power Ups", "Photos", "Music", "Outfits" };
static const char k_cat_code[4] = { 'P', 'G', 'M', 'O' };

static u32 rgba(u8 r, u8 g, u8 b, float a) { return C2D_Color32(r, g, b, (u8)(a < 0.0f ? 0.0f : (a > 1.0f ? 255.0f : a * 255.0f))); }
static u32 white(float a)  { return rgba(0xF4, 0xF4, 0xF8, a); }
static u32 gray(float a)   { return rgba(0x86, 0x86, 0x92, a); }
static u32 gold(float a)   { return rgba(0xFF, 0xC8, 0x3C, a); }
static u32 shop_orange(float a) { return rgba(0xFF, 0x96, 0x30, a); }

/* ---- text ---- */
static void T(bb_face_t face, int size, float x, float y, u32 c, u32 align, const char* s) {
    bb_font_draw(face, size, x, y, c, align, s, ZT);
}
static void TS(bb_face_t face, int size, float x, float y, u32 c, u32 align, const char* s) {   /* small caps */
    bb_font_draw_smallcaps(face, size, x, y, c, align, s, ZT);
}

/* Word wrap by pixel width; returns the line count. */
static int wrap_px(bb_face_t face, int size, const char* src, float maxw, char out[][96], int max_lines) {
    int n = 0;
    const char* p = src;
    while (*p && n < max_lines) {
        while (*p == ' ') p++;
        if (!*p) break;
        int len = (int)strlen(p);
        int cut = len;
        if (bb_font_width(face, size, p) > maxw) {
            cut = 0;
            int last_space = -1;
            for (int i = 0; i < len && i < 94; i++) {
                char tmp[96];
                snprintf(tmp, sizeof(tmp), "%.*s", i + 1, p);
                if (bb_font_width(face, size, tmp) > maxw) break;
                cut = i + 1;
                if (p[i] == ' ') last_space = i;
            }
            if (last_space > 0) cut = last_space;
            if (cut <= 0) cut = 1;
        }
        snprintf(out[n++], 96, "%.*s", cut, p);
        p += cut;
    }
    return n;
}

static int draw_wrapped(bb_face_t face, int size, float x, float y, float line_h, float maxw, u32 c, u32 align, const char* text, int max_lines) {
    char lines[8][96];
    int n = wrap_px(face, size, text, maxw, lines, max_lines < 8 ? max_lines : 8);
    for (int i = 0; i < n; i++) T(face, size, x, y + (float)i * line_h, c, align, lines[i]);
    return n;
}

/* ---- shapes ---- */
/* One quarter disc as a triangle fan (q: 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left). Translucent panels must not overlap
 * themselves, or the doubled alpha shows as dark dots at the corners. */
static void corner(float cx, float cy, float r, int q, u32 c, float z) {
    const int seg = r > 8.0f ? 6 : 4;
    const float a0 = 3.14159265f * (1.0f + 0.5f * (float)q), quarter = 1.5707963f;
    for (int i = 0; i < seg; i++) {
        const float s0 = a0 + quarter * (float)i / (float)seg, s1 = a0 + quarter * (float)(i + 1) / (float)seg;
        C2D_DrawTriangle(cx, cy, c, cx + r * cosf(s0), cy + r * sinf(s0), c, cx + r * cosf(s1), cy + r * sinf(s1), c, z);
    }
}

static void rrect(float x, float y, float w, float h, float r, u32 c, float z) {
    if (r * 2.0f > w) r = w * 0.5f;
    if (r * 2.0f > h) r = h * 0.5f;
    C2D_DrawRectSolid(x + r, y, z, w - 2.0f * r, h, c);
    C2D_DrawRectSolid(x, y + r, z, r, h - 2.0f * r, c);
    C2D_DrawRectSolid(x + w - r, y + r, z, r, h - 2.0f * r, c);
    corner(x + r, y + r, r, 0, c, z);
    corner(x + w - r, y + r, r, 1, c, z);
    corner(x + w - r, y + h - r, r, 2, c, z);
    corner(x + r, y + h - r, r, 3, c, z);
}

static void frame(float x, float y, float w, float h, float r, u32 border, u32 fill, float z) {
    rrect(x, y, w, h, r, border, z);
    rrect(x + 1.5f, y + 1.5f, w - 3.0f, h - 3.0f, r - 1.0f > 1.0f ? r - 1.0f : 1.0f, fill, z + 0.002f);
}

/* Black side panel with a torn (zig-zag) edge, like the Extras / Achievements panels. edge_x is where the teeth point to. */
static void torn_panel(float x0, float x1, bool teeth_right, float z, u32 col) {
    C2D_DrawRectSolid(x0, 0, z, x1 - x0, 240, col);
    for (int k = 0; k < 20; k++) {
        float y = (float)k * 12.0f;
        float base = teeth_right ? x1 : x0;
        float tip = teeth_right ? x1 + 7.0f : x0 - 7.0f;
        C2D_DrawTriangle(base, y, col, tip, y + 6.0f + (float)(k % 3), col, base, y + 12.0f, col, z);
    }
}

static void free_sheet(C2D_SpriteSheet* sh) {
    if (*sh) {
        C2D_SpriteSheetFree(*sh);
        *sh = NULL;
    }
}

static void set_sub(Tex3DS_SubTexture* st, float x, float y, float w, float h, float tw, float th) {
    st->width = (u16)w;
    st->height = (u16)h;
    st->left = x / tw;
    st->right = (x + w) / tw;
    st->top = 1.0f - y / th;
    st->bottom = 1.0f - (y + h) / th;
}

/* Draw a single-image sheet fitted inside the (x,y,mw,mh) box, keeping its aspect (the t3x carries its own sub-rect). */
static void draw_fit(C2D_SpriteSheet sh, float x, float y, float mw, float mh, float z) {
    if (!sh) {
        return;
    }
    C2D_Image img = C2D_SpriteSheetGetImage(sh, 0);
    float w = (float)img.subtex->width, h = (float)img.subtex->height;
    float k = mw / w < mh / h ? mw / w : mh / h;
    C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
    C2D_SetTintMode(C2D_TintMult);
    C2D_DrawParams dp;
    dp.pos.x = x + (mw - w * k) * 0.5f; dp.pos.y = y + (mh - h * k) * 0.5f; dp.pos.w = w * k; dp.pos.h = h * k;
    dp.center.x = 0; dp.center.y = 0; dp.depth = z; dp.angle = 0;
    C2D_DrawImage(img, &dp, NULL);
}

static int shop_count_cat(const bb_screens_t* sc, char code, int* idx) {
    int n = 0;
    for (int i = 0; i < sc->ex->shop_count; i++) {
        if (sc->ex->shop[i].cat == code) idx[n++] = i;
    }
    return n;
}

static void say(bb_screens_t* sc, const char* text) {
    snprintf(sc->speech, sizeof(sc->speech), "%s", text);
    sc->speech_t0 = bb_ui_time();
    sc->speech_blips = 0;
    sc->idle_t0 = sc->speech_t0;
}

static void say_pool(bb_screens_t* sc, const char* kind) {
    const char* l = bb_extras_pool_line(sc->ex, kind, &sc->rng);
    if (l[0]) say(sc, l);
}

static void load_jasmine(bb_screens_t* sc) {
    int outfit = sc->save->player.outfit;
    if (sc->jasmine && sc->jasmine_outfit == outfit) {
        return;
    }
    free_sheet(&sc->jasmine);
    char name[32];
    snprintf(name, sizeof(name), "jasmine_%d.t3x", outfit);
    sc->jasmine = bb_extras_load_sheet(name);
    if (!sc->jasmine) sc->jasmine = bb_extras_load_sheet("jasmine_0.t3x");
    sc->jasmine_outfit = outfit;
}

static void xm_free(void);                       /* the Extras menu art (below) */
static void grant_seasonal(bb_screens_t* sc);    /* the seasonal outfits (below) */

static void free_gallery(bb_screens_t* sc) {
    for (int i = 0; i < BB_MAX_GALLERY; i++) free_sheet(&sc->gthumb[i]);
    sc->gthumb_loaded = false;
}

void bb_screens_init(bb_screens_t* sc, bb_extras_t* ex, bb_save_t* save) {
    memset(sc, 0, sizeof(*sc));
    sc->ex = ex;
    sc->save = save;
    sc->rng = 0x1234ABCDu;
    sc->detail_key = -1;
    sc->jasmine_outfit = -1;
}

void bb_screens_close(bb_screens_t* sc) {
    free_sheet(&sc->jasmine);
    free_sheet(&sc->detail);
    free_sheet(&sc->atlas);
    free_gallery(sc);
    xm_free();
    bb_coll_close();
    bb_gal_close();
    bb_music_close();
    bb_sv_close();
    sc->detail_key = -1;
    sc->jasmine_outfit = -1;
}

/* Collectables with non-level conditions (class/items.gd + the descriptions in the reference data). */
static void sync_collectables(bb_screens_t* sc) {
    bb_player_t* p = &sc->save->player;
    bool all_levels = sc->level_count > 0;
    for (int i = 0; i < sc->level_count; i++) if (!bb_player_has_item(p, sc->level_enums[i])) all_levels = false;
    if (all_levels && bb_player_add_item(p, 707)) sc->dirty = true;                 /* Spray Bottle: beat every level */
    if (p->exp > 500 && bb_player_add_item(p, BB_ITEM_COLLECT_LICENSE)) sc->dirty = true;   /* Protag's License: main_menu.gd EXP > 500 */
    bool all_shop = sc->ex->shop_count > 0, all_music = true;
    for (int i = 0; i < sc->ex->shop_count; i++) {
        const bb_shop_item_t* it = &sc->ex->shop[i];
        if (it->cat == 'P') continue;   /* power-ups are consumables */
        if (!bb_player_has_item(p, it->item)) {
            all_shop = false;
            if (it->cat == 'M') all_music = false;
        }
    }
    if (all_shop && bb_player_add_item(p, 703)) sc->dirty = true;                   /* Backpack: buy all shop items */
    if (all_music && bb_player_add_item(p, 702)) sc->dirty = true;                  /* Headphones: all music */
    if (bb_player_add_item(p, 719)) sc->dirty = true;                               /* Condoms: free in the shop */
}

void bb_screens_open(bb_screens_t* sc, sc_root_t root) {
    sc->result = 0;
    sc->chosen_powerup = 0;
    switch (root) {
        case SC_SHOP:
            sc->node = SCN_SHOP;
            sc->cursor[SCN_SHOP] = 0;
            snprintf(sc->talk_set, sizeof(sc->talk_set), "set_dialogue_default");
            sc->speech[0] = 0;
            sc->shop_cat = 0;
            sc->shop_open_t = sc->list_t0 = bb_ui_time();
            sc->react_kind = 0;
            sc->booping = false;
            sc->honk_amount = 0;   /* shop_honk.gd honk_amount lives with the shop scene */
            for (int i = 0; i < 4; i++) sc->opt_w[i] = 0.0f;
            sc->cash_from = sc->cash_to = (int)sc->save->player.money;
            sc->cash_t0 = -10.0f;
            sc->react_t0 = -10.0f;
            sc->mouth_n = sc->mouth_shape = sc->mouth_last = 0;
            say_pool(sc, "entering");
            load_jasmine(sc);
            sync_collectables(sc);
            grant_seasonal(sc);
            break;
        case SC_EXTRAS:       sc->node = SCN_EXTRAS; sc->cursor[SCN_EXTRAS] = 0; sync_collectables(sc); break;
        case SC_ACHIEVEMENTS: {   /* achievements.gd refresh: check_unlocked_achievements when the menu opens */
            int fresh[8];
            if (bb_ach_check_stats(sc->save, fresh, 8) > 0) sc->dirty = true;
            sc->node = SCN_ACH; sc->cursor[SCN_ACH] = 0;
            break;
        }
        case SC_POWERUP:      sc->node = SCN_POWERUP; sc->cursor[SCN_POWERUP] = 0; break;
    }
}

/* ---------------- Theater reels ---------------- */
void bb_screens_add_reel_stage(bb_screens_t* sc, const char* id, const char* character, const char* title, const char* act, int level_enum) {
    if (sc->reel_stages >= SC_MAX_REEL_STAGES) return;
    const int i = sc->reel_stages++;
    snprintf(sc->reel_id[i], sizeof(sc->reel_id[0]), "%s", id);
    snprintf(sc->reel_char[i], sizeof(sc->reel_char[0]), "%s", character);
    snprintf(sc->reel_title[i], sizeof(sc->reel_title[0]), "%s", title);
    snprintf(sc->reel_act[i], sizeof(sc->reel_act[0]), "%s", act);
    sc->reel_enum[i] = level_enum;
}

/* CutsceneReel.gd / theater.tscn: a reel unlocks when unlocked_items has its unlock_id, which is the level's enum for both its
 * pre and post scenes (Prologue / Nice Work: 501), i.e. once that level has been cleared. */
bool bb_screens_reel_open(const bb_screens_t* sc, int pick) {
    const int st = pick / 2;
    if (st < 0 || st >= sc->reel_stages) return false;
    if (sc->save->settings.unlock_all) return true;
    return sc->reel_enum[st] > 0 && bb_player_has_item(&sc->save->player, sc->reel_enum[st]);
}

/* lore_character.gd: each character's outfit (0 clothed, 1 alt = lingerie, 2 nude) is kept while browsing, like CharacterLoreData.outfit_index */
static int   g_outfit[BB_MAX_LORE];
static float g_outfit_t = -10.0f;      /* the last change: pulse 1.1 -> 1.0 (0.2 s, back ease) */
static float g_char_t = -10.0f;        /* the last character switch: the "appear" */

#define ACH_COLS 5   /* achievements grid (achievements.tscn GridContainer, 5 columns) */
#define GAL_COLS 3   /* gallery grid on the bottom screen */

/* ---------------- list helpers ---------------- */
static int move_cursor(int cur, int count, u32 kRep) {
    if (count <= 0) return 0;
    if (kRep & KEY_DUP) cur = (cur + count - 1) % count;
    if (kRep & KEY_DDOWN) cur = (cur + 1) % count;
    return cur;
}

/* grid cursor: left/right step 1, up/down step `cols`, clamped (no wrap) */
static int move_grid(int cur, int count, int cols, u32 kRep) {
    if (count <= 0) return 0;
    if ((kRep & KEY_DLEFT) && cur > 0) cur--;
    if ((kRep & KEY_DRIGHT) && cur + 1 < count) cur++;
    if ((kRep & KEY_DUP) && cur - cols >= 0) cur -= cols;
    if ((kRep & KEY_DDOWN) && cur + cols < count) cur += cols;
    return cur;
}

static int talk_count_set(const bb_screens_t* sc, int* idx) {
    int n = 0;
    for (int i = 0; i < sc->ex->talk_count; i++) {
        if (strcmp(sc->ex->talk[i].set, sc->talk_set) == 0) idx[n++] = i;
    }
    return n;
}

static void pick_response(bb_screens_t* sc, const char* responses) {
    char tmp[240];
    snprintf(tmp, sizeof(tmp), "%s", responses);
    char* parts[12];
    int n = 0;
    char* p = tmp;
    while (n < 12) {
        parts[n++] = p;
        char* sep = strstr(p, ";;");
        if (!sep) break;
        *sep = 0;
        p = sep + 2;
    }
    if (n > 0 && parts[0][0]) {
        sc->rng = sc->rng * 1664525u + 1013904223u;
        say(sc, parts[(sc->rng >> 8) % (uint32_t)n]);
    }
}

/* shop_clothes_menu / wearable_outfits_default.tres: vest (the default), tank top, staff uniform, skull bikini, tits out, piercings. */
/* clothes_menu.tscn outfit_menus: page 0 "Outfits" (wearable_outfits_default, 6 slots), page 1 "Seasonal" (wearable_outfits_seasonal:
 * the Nurse, OUTFIT_JASMINE_NURSE 1106). */
#define OUTFIT_PAGES 2
static const char* const k_outfit_page[OUTFIT_PAGES] = { "Outfits", "Seasonal" };
static int outfit_page_rows(int page, int* rows) {
    static const int k_rows[6] = { 0, 1101, 1104, 1105, 1102, 1103 };
    if (page == 1) { rows[0] = 1106; return 1; }
    for (int i = 0; i < 6; i++) rows[i] = k_rows[i];
    return 6;
}
static int outfit_rows(const bb_screens_t* sc, int* rows) { return outfit_page_rows(sc->outfit_page % OUTFIT_PAGES, rows); }

/* shop.gd _ready: during the Halloween event (Calendar.is_halloween: Oct 23 - Nov 7) entering the shop grants the Nurse outfit.
 * The port also grants it with unlock-all on (the test build), so the seasonal page is wearable there. */
static void grant_seasonal(bb_screens_t* sc) {
    const time_t now = time(NULL);
    const struct tm* t = localtime(&now);
    const int md = t ? (t->tm_mon + 1) * 100 + t->tm_mday : 0;
    const bool halloween = (md >= 1023 && md <= 1031) || (md >= 1101 && md <= 1107);
    /* shop.gd: entering the shop in the Halloween theme unlocks the Nurse (the theme can also be chosen with Change Theme) */
    if ((halloween || bb_theme_halloween()) && bb_player_add_item(&sc->save->player, 1106)) sc->dirty = true;   /* shop.gd: only the season / theme */
}

/* ---------------- Jasmine's Shop: reactions, speech and sounds ---------------- */
enum { REACT_NONE = 0, REACT_BUY, REACT_DENY, REACT_OUTFIT };

static void react(bb_screens_t* sc, int kind) {
    sc->react_kind = kind;
    sc->react_t0 = bb_ui_time();
}

static float rnd01(bb_screens_t* sc) {
    sc->rng = sc->rng * 1664525u + 1013904223u;
    return (float)((sc->rng >> 8) & 0xFFFFu) / 65535.0f;
}

/* resources/wearable_outfits (one .tres per outfit): vest.tres plays outfit_shirt, Tits / piercings have their own, everything else outfit_no_shirt. */
static int equip_sfx(int item) {
    switch (item) {
        case 1102: return BB_UISFX_OUTFIT_TITS;
        case 1103: return BB_UISFX_OUTFIT_PIERCINGS;
        case 1101: case 1104: case 1105: return BB_UISFX_OUTFIT_NOSHIRT;
        default: return BB_UISFX_OUTFIT_VEST;
    }
}

/* SpeechBubble.gd: the line is revealed over 0.8 s with one voice blip (pitch 0.98-1.07) per two visible characters; Jasmine's mouth
 * changes shape on every second blip; an idle line is spoken every 20 s while the top-level shop menu is active. */
static void speech_tick(bb_screens_t* sc) {
    const int len = (int)strlen(sc->speech);
    float ratio = (bb_ui_time() - sc->speech_t0) / 0.8f;
    if (ratio > 1.0f) ratio = 1.0f;
    const int vis = (int)(ratio * (float)len);
    if (vis > sc->speech_blips && vis % 2 == 0) {
        sc->speech_blips = vis;
        bb_uisfx_play_pitch(BB_UISFX_VOICE, 0.98f + 0.09f * rnd01(sc));
        if (++sc->mouth_n % 2 == 0) {
            int next = (int)(rnd01(sc) * 3.999f);
            if (next == sc->mouth_last) next = (next + 1 + (int)(rnd01(sc) * 2.999f)) % 4;
            sc->mouth_shape = sc->mouth_last = next;
        }
    }
    if (vis > sc->speech_blips) sc->speech_blips = vis;
    if (sc->node == SCN_SHOP && bb_ui_time() - sc->idle_t0 > 20.0f) say_pool(sc, "idle");
}

static void exit_talk(bb_screens_t* sc) {   /* shop_talk_menu.exit_talk */
    sc->node = SCN_SHOP;
    say(sc, "Oh you're done??? What a fun conversation...");
}

bool bb_screens_is_shop(const bb_screens_t* sc) {
    return sc->node >= SCN_SHOP && sc->node <= SCN_SHOP_OUTFIT;
}

/* shop_honk.gd _on_button_up: every honk counts (STAT_JASMINE_NOSE_HONKS) and re-checks the achievements; misc_honk at pitch
 * 1 + honk / 20 for the first seven, "Stop that." at 2, "Seriously." at 5; from the seventh on the pitch stays at 1.3 and she either
 * says "Buzz off" (photo already owned) or gives you the photo (GALLERY_JASMINE_TITFUCK) with "I hope you're proud of yourself...". */
static void shop_honk(bb_screens_t* sc) {
    bb_player_t* p = &sc->save->player;
    bb_player_stat_add(p, BB_STAT_NOSE_HONKS, 1);
    int fresh[4];
    if (bb_ach_check_stats(sc->save, fresh, 4) > 0) bb_uisfx_play(BB_UISFX_ACH);
    sc->dirty = true;
    if (sc->honk_amount < 7) {
        bb_uisfx_play_pitch(BB_UISFX_HONK, (float)sc->honk_amount / 20.0f + 1.0f);
        sc->honk_amount++;
        if (sc->honk_amount == 2) say(sc, "Stop that.");
        else if (sc->honk_amount == 5) say(sc, "Seriously.");
    } else {
        bb_uisfx_play_pitch(BB_UISFX_HONK, 6.0f / 20.0f + 1.0f);
        if (bb_player_has_item(p, BB_ITEM_GALLERY_JASMINE_TITFUCK)) { say(sc, "Buzz off"); return; }
        bb_player_add_item(p, BB_ITEM_GALLERY_JASMINE_TITFUCK);   /* Items.unlock_gallery_item */
        bb_uisfx_play(BB_UISFX_UNLOCK);
        say(sc, "I hope you're proud of yourself...");
    }
}

/* Touch on the bottom-screen UIs: an item zone selects, a tap on the selected one presses it (A); back / arrows act at once. */
/* Theater: a vertical drag scrolls the reel list (g_th_off px over the cursor position); a row acts on release, not on touch-down,
 * so a drag never starts a cutscene. The stylus is read here (hidScanInput has run this frame). */
static struct { bool down, drag; int y0, z0; float s0; } g_thd;
static float g_th_off = 0.0f;

void bb_screens_touch(bb_screens_t* sc, bool tap, int tx, int ty, u32* kDown) {
    if (!tap || !kDown) return;
    const int z = bb_ui_zone_hit(tx, ty);
    if (z < 0) return;
    if (sc->node == SCN_THEATER && z >= BB_ZONE_LEVEL && z < BB_ZONE_ACT) {   /* decided on release (bb_screens_update) */
        g_thd.down = true; g_thd.drag = false; g_thd.y0 = ty; g_thd.z0 = z; g_thd.s0 = g_th_off;
        return;
    }
    if (sc->node == SCN_SHOP && sc->booping) {   /* Boop: her nose honks, back leaves */
        if (z == BB_ZONE_HANDSFREE) shop_honk(sc);
        else if (z == BB_ZONE_BACK) *kDown |= KEY_B;
        return;
    }
    if (z == BB_ZONE_BACK) { *kDown |= KEY_B; return; }
    if (z == BB_ZONE_PREV) { *kDown |= KEY_L; return; }
    if (z == BB_ZONE_NEXT) { *kDown |= KEY_R; return; }
    if (z >= BB_ZONE_ACT && z < BB_ZONE_ACT + 4 && sc->node == SCN_SHOP_LIST) {   /* category tabs */
        if (z - BB_ZONE_ACT != sc->shop_cat) {
            sc->shop_cat = z - BB_ZONE_ACT;
            sc->cursor[SCN_SHOP_LIST] = 0;
            sc->list_t0 = bb_ui_time();
            bb_uisfx_play(BB_UISFX_TOUCH);
        }
        return;
    }
    if (z >= BB_ZONE_LEVEL && z < BB_ZONE_ACT) {
        const int i = z - BB_ZONE_LEVEL;
        int* c = &sc->cursor[sc->node];
        if (*c == i) *kDown |= KEY_A;
        else { *c = i; bb_uisfx_play(sc->node == SCN_THEATER ? BB_UISFX_TICK : BB_UISFX_TOUCH); }
    }
}

int bb_screens_update(bb_screens_t* sc, u32 kDown, u32 kRep) {
    bb_player_t* p = &sc->save->player;
    /* the circle pad moves every cursor the D-pad moves */
    static const u32 cpad[4][2] = { { KEY_CPAD_UP, KEY_DUP }, { KEY_CPAD_DOWN, KEY_DDOWN }, { KEY_CPAD_LEFT, KEY_DLEFT }, { KEY_CPAD_RIGHT, KEY_DRIGHT } };
    for (int k = 0; k < 4; k++) {
        if (kDown & cpad[k][0]) kDown |= cpad[k][1];
        if (kRep & cpad[k][0]) kRep |= cpad[k][1];
    }
    int idx[SC_MAX_ROWS];
    if (bb_screens_is_shop(sc)) speech_tick(sc);
    switch (sc->node) {
        case SCN_SHOP: {
            if (sc->booping) {   /* Boop: only the nose (touch) and back */
                if (kDown & KEY_B) { sc->booping = false; bb_uisfx_play(BB_UISFX_BACK); }
                break;
            }
            const int before = sc->cursor[SCN_SHOP];
            sc->cursor[SCN_SHOP] = move_cursor(before, 4, kDown);
            if (sc->cursor[SCN_SHOP] != before) bb_uisfx_play(BB_UISFX_TOUCH);          /* UiButton focus */
            if (kDown & KEY_B) { sc->result = 1; break; }                                /* leave_shop: silent */
            if (kDown & KEY_A) {
                switch (sc->cursor[SCN_SHOP]) {
                    case 0:   /* shop_options._on_buy_button_up: ui_click, the "buying" line, straight onto the Power Ups tab */
                        sc->node = SCN_SHOP_LIST; sc->shop_cat = 0; sc->cursor[SCN_SHOP_LIST] = 0; sc->list_t0 = bb_ui_time();
                        say_pool(sc, "buying"); bb_uisfx_play(BB_UISFX_CLICK);
                        break;
                    case 1:   /* _on_talk_button_up */
                        sc->node = SCN_SHOP_TALK; sc->cursor[SCN_SHOP_TALK] = 0;
                        snprintf(sc->talk_set, sizeof(sc->talk_set), "set_dialogue_default");
                        say(sc, "What do you want?"); bb_uisfx_play(BB_UISFX_CLICK);
                        break;
                    case 3:   /* Boop (port): Jasmine up close on the touch screen, honk her nose (shop_honk.gd) */
                        sc->booping = true;
                        bb_uisfx_play(BB_UISFX_CLICK);
                        break;
                    default: {   /* _on_outfit_button_up: silent, the speech bubble hides */
                        int rows[6];
                        sc->node = SCN_SHOP_OUTFIT;
                        sc->cursor[SCN_SHOP_OUTFIT] = 0;
                        sc->outfit_page = 0;
                        for (int pg = 0; pg < OUTFIT_PAGES; pg++) {
                            const int n = outfit_page_rows(pg, rows);
                            for (int i = 0; i < n; i++) if (rows[i] == p->outfit) { sc->outfit_page = pg; sc->cursor[SCN_SHOP_OUTFIT] = i; }
                        }
                        break;
                    }
                }
            }
            break;
        }
        case SCN_SHOP_LIST: {
            const int n = shop_count_cat(sc, k_cat_code[sc->shop_cat], idx);
            const int before = sc->cursor[SCN_SHOP_LIST];
            sc->cursor[SCN_SHOP_LIST] = move_cursor(before, n, kRep);
            if (sc->cursor[SCN_SHOP_LIST] != before) bb_uisfx_play(BB_UISFX_TOUCH);
            if (kDown & KEY_B) { sc->node = SCN_SHOP; break; }                           /* buy_menu.exit_buy: silent */
            if (kDown & (KEY_L | KEY_R)) {   /* shop_categories.set_buy_menu_category: sfx_menu_touch */
                sc->shop_cat = (sc->shop_cat + ((kDown & KEY_R) ? 1 : 3)) % 4;
                sc->cursor[SCN_SHOP_LIST] = 0;
                sc->list_t0 = bb_ui_time();
                bb_uisfx_play(BB_UISFX_TOUCH);
                break;
            }
            if ((kDown & KEY_A) && n > 0) {
                const bb_shop_item_t* it = &sc->ex->shop[idx[sc->cursor[SCN_SHOP_LIST]]];
                bb_buy_result_t r = bb_shop_buy(sc->save, it->item, it->price, it->req_exp);
                if (r == BB_BUY_OK) {
                    sc->dirty = true;
                    bb_uisfx_play(BB_UISFX_BUY);
                    if (it->item == 101) say(sc, "Skill issue.");
                    else say_pool(sc, "purchase_success");
                    react(sc, REACT_BUY);
                } else if (r == BB_BUY_OWNED) {
                    bb_uisfx_play(BB_UISFX_ERROR);
                    say_pool(sc, "purchase_sold_out");
                    react(sc, REACT_DENY);
                } else if (r == BB_BUY_NO_MONEY) {
                    bb_uisfx_play(BB_UISFX_LOCK);
                    say_pool(sc, "purchase_insufficient_funds");
                    react(sc, REACT_DENY);
                } else {
                    bb_uisfx_play(BB_UISFX_LOCK);                                        /* ShopItem: rank-locked press is only ui_lock */
                }
            }
            break;
        }
        case SCN_SHOP_TALK: {
            const int n = talk_count_set(sc, idx);
            const int before = sc->cursor[SCN_SHOP_TALK];
            sc->cursor[SCN_SHOP_TALK] = move_cursor(before, n, kDown);
            if (sc->cursor[SCN_SHOP_TALK] != before) bb_uisfx_play(BB_UISFX_TOUCH);
            if (kDown & KEY_B) { exit_talk(sc); break; }
            if ((kDown & KEY_A) && n > 0) {
                const bb_talk_t* t = &sc->ex->talk[idx[sc->cursor[SCN_SHOP_TALK]]];
                if (t->responses[0] == 0) { exit_talk(sc); break; }                      /* "Nevermind." */
                pick_response(sc, t->responses);
                if (t->next[0]) snprintf(sc->talk_set, sizeof(sc->talk_set), "%s", t->next);
                else snprintf(sc->talk_set, sizeof(sc->talk_set), "set_dialogue_default");
                sc->cursor[SCN_SHOP_TALK] = 0;
            }
            break;
        }
        case SCN_SHOP_OUTFIT: {
            int rows[6];
            const int n = outfit_rows(sc, rows);
            const int before = sc->cursor[SCN_SHOP_OUTFIT];
            sc->cursor[SCN_SHOP_OUTFIT] = move_grid(before, n, 3, kRep);
            if (sc->cursor[SCN_SHOP_OUTFIT] != before) bb_uisfx_play(BB_UISFX_TOUCH);   /* ShopOutfit.highlight */
            if (kDown & KEY_B) { sc->node = SCN_SHOP; say_pool(sc, "idle"); break; }     /* exit_clothes_menu -> idle line */
            if (kDown & (KEY_L | KEY_R)) {   /* _on_arrow_button_pressed: next / previous outfit menu, sfx_analog_click */
                sc->outfit_page = (sc->outfit_page + ((kDown & KEY_R) ? 1 : OUTFIT_PAGES - 1)) % OUTFIT_PAGES;
                sc->cursor[SCN_SHOP_OUTFIT] = 0;
                sc->list_t0 = bb_ui_time();
                bb_uisfx_play(BB_UISFX_ANALOG_CLICK);
                break;
            }
            if (kDown & KEY_A) {
                const int item = rows[sc->cursor[SCN_SHOP_OUTFIT]];
                if (item != 0 && !bb_player_has_item(p, item)) { bb_uisfx_play(BB_UISFX_LOCK); break; }
                bb_uisfx_play_pitch(equip_sfx(item), 1.0f + 0.2f * rnd01(sc) - 0.2f * rnd01(sc));   /* shop_outfit_icon.equip */
                p->outfit = (uint16_t)item;
                sc->dirty = true;
                load_jasmine(sc);
                react(sc, REACT_OUTFIT);
            }
            break;
        }
        case SCN_EXTRAS: {
            /* Music, Gallery, Theater, Characters, Scene Viewer, Collection (original order) */
            const int before = sc->cursor[SCN_EXTRAS];
            sc->cursor[SCN_EXTRAS] = move_cursor(before, 6, kDown);
            if (sc->cursor[SCN_EXTRAS] != before) bb_uisfx_play(BB_UISFX_TOUCH);        /* UiButton focus */
            sc->note[0] = 0;
            if (kDown & KEY_B) { sc->result = 1; break; }
            if (kDown & KEY_A) {
                bb_uisfx_play(BB_UISFX_ACCEPT);
                switch (sc->cursor[SCN_EXTRAS]) {
                    case 1: sc->node = SCN_GALLERY; bb_gal_open(sc->save); break;   /* gallery_ui.c (main.c routes its input) */
                    case 2: sc->node = SCN_THEATER; break;   /* MenuCache.theater_menu_index: the reel cursor is remembered */
                    case 3: sc->node = SCN_CHARS; sc->page = 0; break;
                    case 5: sc->node = SCN_COLLECT; bb_coll_open(sc->ex, sc->save); break;   /* collection_ui.c (main.c routes its input) */
                    case 0: sc->node = SCN_MUSIC; bb_music_open(sc->save); break;        /* sound_test.tscn */
                    case 4: sc->node = SCN_SCENEVIEW; bb_sv_open(sc->save); break;   /* scene_viewer.tscn */
                    default: break;
                }
            }
            break;
        }
        case SCN_THEATER: {   /* theater.gd: A plays the reel (ui_accept via load_scene) or ui_lock; back returns to Extras silently */
            const int n = sc->reel_stages * 2;
            int c = sc->cursor[SCN_THEATER];
            if (n > 0) {
                c = move_cursor(c, n, kRep);
                if (kRep & (KEY_DLEFT | KEY_DRIGHT)) c ^= 1;   /* before <-> after of the same level */
                if (c >= n) c = n - 1;
                if (c != sc->cursor[SCN_THEATER]) { bb_uisfx_play(BB_UISFX_TICK); g_th_off = 0.0f; }
                sc->cursor[SCN_THEATER] = c;
            }
            if (g_thd.down) {   /* the stylus: drag scrolls, a release without a drag selects / plays the row */
                const bool held = (hidKeysHeld() & KEY_TOUCH) != 0;
                if (held) {
                    touchPosition tp;
                    hidTouchRead(&tp);
                    const int dy = (int)tp.py - g_thd.y0;
                    if (dy > 6 || dy < -6) g_thd.drag = true;
                    if (g_thd.drag) g_th_off = g_thd.s0 - (float)dy;
                } else {
                    g_thd.down = false;
                    const int i = g_thd.z0 - BB_ZONE_LEVEL;
                    if (!g_thd.drag && i >= 0 && i < n) {
                        if (i == c) kDown |= KEY_A;
                        else { c = i; sc->cursor[SCN_THEATER] = c; bb_uisfx_play(BB_UISFX_TICK); }
                    }
                }
            }
            if (kDown & KEY_B) { sc->node = SCN_EXTRAS; break; }
            if ((kDown & KEY_A) && n > 0) {
                if (!bb_screens_reel_open(sc, c)) { bb_uisfx_play(BB_UISFX_LOCK); break; }
                bb_uisfx_play(BB_UISFX_ACCEPT);
                sc->theater_pick = c;
                sc->result = 3;
            }
            break;
        }
        case SCN_CHARS: {
            int n = sc->ex->lore_count;
            if (n > 0 && (kDown & (KEY_R | KEY_DRIGHT))) { sc->page = (sc->page + 1) % n; bb_uisfx_play(BB_UISFX_TICK); }          /* lore.gd:_on_right_button_up */
            if (n > 0 && (kDown & (KEY_L | KEY_DLEFT))) { sc->page = (sc->page + n - 1) % n; bb_uisfx_play(BB_UISFX_TICK); }    /* _on_left_button_up */
            if (kDown & KEY_B) sc->node = SCN_EXTRAS;
            if ((kDown & KEY_A) && n > 0) {   /* lore_character: ui_accept / a click changes the outfit (outfit_no_shirt, pitch 0.9-1.1) */
                const int li = sc->page % n;
                g_outfit[li] = (g_outfit[li] + 1) % 3;
                if (strcmp(sc->ex->lore[li].name, "Dawna") == 0 && bb_player_add_item(p, BB_ITEM_COLLECT_DAWNA_PHOTO)) {   /* lore_character.gd */
                    sc->dirty = true;
                    bb_uisfx_play(BB_UISFX_UNLOCK);
                }
                g_outfit_t = bb_ui_time();
                bb_uisfx_play_pitch(BB_UISFX_OUTFIT_NOSHIRT, 0.9f + 0.2f * rnd01(sc));
            }
            break;
        }
        case SCN_ACH: {
            const int before = sc->cursor[SCN_ACH];
            sc->cursor[SCN_ACH] = move_grid(before, BB_ACH_COUNT, ACH_COLS, kRep);
            if (sc->cursor[SCN_ACH] != before) bb_uisfx_play(BB_UISFX_TOUCH);
            if (kDown & KEY_B) sc->result = 1;
            break;
        }
        case SCN_POWERUP: {
            int rows[6], n = 0;
            rows[n++] = 0;
            for (int it = BB_ITEM_POWERUP_EXTRA_LIVES; it <= BB_ITEM_POWERUP_HARDCORE; it++) {
                if (bb_player_has_item(p, it)) rows[n++] = it;
            }
            const int before = sc->cursor[SCN_POWERUP];
            int c = before;
            if ((kRep & (KEY_DLEFT | KEY_CPAD_LEFT | KEY_DUP)) && c > 0) c--;
            if ((kRep & (KEY_DRIGHT | KEY_CPAD_RIGHT | KEY_DDOWN)) && c + 1 < n) c++;
            if (c >= n) c = n - 1;
            sc->cursor[SCN_POWERUP] = c;
            if (c != before) bb_uisfx_play(BB_UISFX_TICK);        /* BaseMenu cursor */
            if (kDown & KEY_B) { bb_uisfx_play(BB_UISFX_BACK); sc->chosen_powerup = -1; sc->result = 1; break; }
            if (kDown & KEY_A) { sc->chosen_powerup = rows[sc->cursor[SCN_POWERUP]]; sc->result = 2; }
            break;
        }
        default:
            sc->result = 1;
            break;
    }
    return sc->result;
}

/* ================================================================ draw ================================================================ */

/* Each character's colour (resources/character_lore/lore_*.tres "color"). */
static void name_color(const char* name, u8* r, u8* g, u8* b) {
    static const struct { const char* n; u8 c[3]; } k[] = {
        { "Cathy", { 0x7E, 0x70, 0x81 } }, { "Claire", { 0xAE, 0x75, 0x6E } }, { "Dawna", { 0xF3, 0xC8, 0x9E } }, { "Gale", { 0xDF, 0x9D, 0x8F } },
        { "Jasmine", { 0xFF, 0x83, 0x93 } }, { "Makayla", { 0xFF, 0xC8, 0x9F } }, { "Zoe", { 0xA2, 0x70, 0xE3 } },
    };
    *r = 0x9A; *g = 0x92; *b = 0xA6;
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) if (strcmp(name, k[i].n) == 0) { *r = k[i].c[0]; *g = k[i].c[1]; *b = k[i].c[2]; }
}

static void back_button(float cx, float cy, float a) {
    bb_art_icon(7, cx, cy, 26.0f, C2D_Color32(255, 255, 255, (u8)(255.0f * a)), ZI + 0.02f);
}

/* ================================================================ Jasmine's Shop: dual screen ================================================================
 * TOP    (400x240): the shop room and a large Jasmine (idle = the original 2-frame body loop, 0.4 s per frame, speech mouth shapes) with
 *                   her speech bubble. She does not move when the menu cursor moves; she slides in once when the shop opens and only
 *                   reacts to events (purchase hop, refusal shake, outfit bump).
 * BOTTOM (320x240): the dimmed room and every interactive element: Buy / Talk / Outfit, category tabs, items with prices, descriptions,
 *                   dialogue choices and the outfit grid. (The one bottom-screen UI the user authorised; see SPRINT_HANDOFF.)
 */

/* Jasmine's body: 267x333 frames at (0,0) and (267,0), mouth shapes 51x54 at (542 + k*55, 0), mouth offset (99,104) inside a frame. */
static void draw_jasmine(bb_screens_t* sc, float x0, float y0, float scale, u8 alpha, int frame, int mouth_shape) {
    if (!sc->jasmine) return;
    if (sc->react_kind == REACT_OUTFIT && !bb_ui_reduce_motion()) {
        /* Jasmine.gd: scale += 0.05 on the new outfit, then back to 0.75 in 0.2 s (back ease out); the node's origin, about which it
         * scales, sits at (310.5, 608) of her 919x1148 frame (Root offset 149, -34): (90, 177) in the port's 267x333 frame */
        const float e = (bb_ui_time() - sc->react_t0) / 0.2f;
        if (e >= 0.0f && e < 1.0f) {
            const float k = (0.80f + (0.75f - 0.80f) * bb_ui_ease_out_back(e)) / 0.75f;
            const float ox = x0 + 90.2f * scale, oy = y0 + 176.6f * scale;
            x0 = ox + (x0 - ox) * k;
            y0 = oy + (y0 - oy) * k;
            scale *= k;
        }
    }
    C2D_Image img = C2D_SpriteSheetGetImage(sc->jasmine, 0);
    const float tw = (float)img.tex->width, th = (float)img.tex->height;
    C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
    C2D_SetTintMode(C2D_TintMult);
    C2D_ImageTint tn;
    C2D_PlainImageTint(&tn, C2D_Color32(255, 255, 255, alpha), 1.0f);
    Tex3DS_SubTexture st;
    set_sub(&st, (float)(frame * 267), 0.0f, 267.0f, 333.0f, tw, th);
    img.subtex = &st;
    C2D_DrawParams dp;
    dp.pos.x = x0; dp.pos.y = y0; dp.pos.w = 267.0f * scale; dp.pos.h = 333.0f * scale;
    dp.center.x = 0; dp.center.y = 0; dp.depth = ZP; dp.angle = 0;
    C2D_DrawImage(img, &dp, &tn);
    if (mouth_shape >= 0) {
        Tex3DS_SubTexture sm;
        set_sub(&sm, (float)(542 + mouth_shape * 55), 0.0f, 51.0f, 54.0f, tw, th);
        img.subtex = &sm;
        dp.pos.x = x0 + 99.0f * scale; dp.pos.y = y0 + 104.0f * scale; dp.pos.w = 51.0f * scale; dp.pos.h = 54.0f * scale;
        dp.depth = ZP + 0.01f;
        C2D_DrawImage(img, &dp, &tn);
    }
}

/* The shop camera (top-screen coordinates, 400x240): the room zoomed 1.16x on its right part (source 54,22 346x207.6 of the room art). A view
 * maps those coordinates to a screen: p' = (p - c) * zoom + o. The top uses the identity; Boop shows it 1.4x around her face. */
typedef struct { float cx, cy, zoom, ox, oy, w, h; } shop_view_t;
static const shop_view_t k_view_top = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 400.0f, 240.0f };
static const shop_view_t k_view_boop = { 190.0f, 104.0f, 1.4f, 160.0f, 120.0f, 320.0f, 240.0f };
#define JAS_X0    109.23f   /* her frame on the top screen: centre x 192, feet 213, scale 0.62 */
#define JAS_Y0    6.54f
#define JAS_SCALE 0.62f

/* source rect of the room art for a top-coordinate rect */
static void room_part(const shop_view_t* v, float tx, float ty, float tw, float th, float dx, float dy, float dw, float dh, u8 bright, float z) {
    const float k = 346.0f / 400.0f;
    bb_art_background_view(BB_BG_SHOP, bright, 54.0f + tx * k, 22.0f + ty * k, tw * k, th * k, dx, dy, dw, dh, z);
    (void)v;
}

static void draw_room(const shop_view_t* v, u8 bright) {
    /* the visible top-coordinate rect, drawn over the whole screen */
    const float tx = v->cx + (0.0f - v->ox) / v->zoom, ty = v->cy + (0.0f - v->oy) / v->zoom;
    room_part(v, tx, ty, v->w / v->zoom, v->h / v->zoom, 0.0f, 0.0f, v->w, v->h, bright, 0.10f);
}

/* her speech bubble (and the JASMINE plate) at screen position bx, by */
static void draw_bubble(bb_screens_t* sc, float bx, float by, float bw) {
    const float t = bb_ui_time();
    const float a = bb_ui_ease_out((t - sc->speech_t0) / 0.2f);
    char lines[8][96];
    const int n = wrap_px(BB_FACE_BOLD, 12, sc->speech, bw - 22.0f, lines, 5);
    const float bh = 15.0f * (float)n + 24.0f;
    const u32 bc = rgba(0x05, 0x04, 0x08, 0.92f * a);
    {   /* name plate: shop.tscn NameLabel - pink fill, black outline, hot-pink slab 10 px (1080p) below it, "hover" 6 px / 2 s ease in-out */
        const float hov = bb_ui_reduce_motion() ? 0.0f : -1.7f * (0.5f - 0.5f * cosf(3.14159265f * t));
        const float nx = bx + 8.0f, ny = by - 29.0f + hov;
        static const float ox[8] = { -2, 0, 2, -2, 2, -2, 0, 2 }, oy[8] = { -2, -2, -2, 0, 0, 2, 2, 2 };
        for (int k = 0; k < 8; k++) {   /* outline around the face and the slab */
            bb_font_draw(BB_FACE_HEAD, 28, nx + ox[k], ny + oy[k], rgba(0, 0, 0, a), C2D_AlignLeft, "JASMINE", ZT);
            bb_font_draw(BB_FACE_HEAD, 28, nx + ox[k], ny + 3.0f + oy[k], rgba(0, 0, 0, a), C2D_AlignLeft, "JASMINE", ZT);
        }
        for (int k = 3; k >= 1; k--) bb_font_draw(BB_FACE_HEAD, 28, nx, ny + (float)k, rgba(0xE0, 0x21, 0x61, a), C2D_AlignLeft, "JASMINE", ZT + 0.001f);
        bb_font_draw(BB_FACE_HEAD, 28, nx, ny, rgba(0xFF, 0x83, 0x93, a), C2D_AlignLeft, "JASMINE", ZT + 0.002f);
    }
    rrect(bx, by, bw, bh, 9.0f, bc, ZP + 0.02f);
    C2D_DrawTriangle(bx + 2.0f, by + bh * 0.5f - 9.0f, bc, bx + 2.0f, by + bh * 0.5f + 9.0f, bc, bx - 11.0f, by + bh * 0.5f + 12.0f, bc, ZP + 0.02f);
    const int total = (int)strlen(sc->speech);
    float reveal = (t - sc->speech_t0) / 0.8f;
    if (reveal > 1.0f) reveal = 1.0f;
    if (reveal < 0.0f) reveal = 0.0f;
    int left = (int)(reveal * (float)total);
    const float y0 = by + (bh - 15.0f * (float)n) * 0.5f;
    for (int i = 0; i < n; i++) {
        const int take = left < (int)strlen(lines[i]) ? left : (int)strlen(lines[i]);
        if (take <= 0) break;
        char part[96];
        snprintf(part, sizeof(part), "%.*s", take, lines[i]);
        T(BB_FACE_BOLD, 12, bx + bw * 0.5f, y0 + (float)i * 15.0f, white(a), C2D_AlignCenter, part);
        left -= (int)strlen(lines[i]) + 1;
    }
}

/* the room, Jasmine, the counter's front edge in front of her, and her bubble, through a view */
static void draw_shop_scene(bb_screens_t* sc, const shop_view_t* v, float bubble_x, float bubble_y, float bubble_w) {
    const float t = bb_ui_time();
    draw_room(v, 255);
    /* a soft pink glow rising from the counter and a vignette at the top, so the room reads as lit from below */
    const float gl = 0.16f + 0.05f * sinf(t * 1.3f);
    const float gy = (130.0f - v->cy) * v->zoom + v->oy;
    C2D_DrawRectangle(0, gy, 0.105f, v->w, v->h - gy, rgba(0xFF, 0x30, 0x90, 0.0f), rgba(0xFF, 0x30, 0x90, 0.0f), rgba(0xFF, 0x30, 0x90, gl), rgba(0xFF, 0x30, 0x90, gl));
    if (v->zoom <= 1.0f) C2D_DrawRectangle(0, 0, 0.105f, v->w, 56.0f, rgba(0, 0, 0, 0.45f), rgba(0, 0, 0, 0.45f), rgba(0, 0, 0, 0.0f), rgba(0, 0, 0, 0.0f));
    /* User directive: her pose is fixed (no idle frames, slide-in, hop, shake, Talk / Outfit drift); the talking mouth shapes and the
     * original's outfit-change bump (Jasmine.gd) animate. */
    const bool talking = sc->speech[0] && (t - sc->speech_t0) < 0.8f && sc->node != SCN_SHOP_OUTFIT;   /* mouth shapes only while a line is typed */
    draw_jasmine(sc, (JAS_X0 - v->cx) * v->zoom + v->ox, (JAS_Y0 - v->cy) * v->zoom + v->oy, JAS_SCALE * v->zoom, 255, 0, talking ? sc->mouth_shape : -1);
    {   /* the counter's front edge (top 204.6..240) in front of her */
        const float dy = (204.6f - v->cy) * v->zoom + v->oy;
        if (dy < v->h) {
            const float k = 346.0f / 400.0f;
            const float tx = v->cx + (0.0f - v->ox) / v->zoom, tw = v->w / v->zoom;
            bb_art_background_view(BB_BG_SHOP, 255, 54.0f + tx * k, 199.0f, tw * k, 30.6f, 0.0f, dy, v->w, 35.4f * v->zoom, ZP + 0.015f);
        }
    }
    if (sc->speech[0] && sc->node != SCN_SHOP_OUTFIT) draw_bubble(sc, bubble_x, bubble_y, bubble_w);
}

static void draw_shop_top(bb_screens_t* sc) {
    if (sc->booping) {   /* Boop: she is on the touch screen; the top keeps the empty room, dimmed */
        draw_room(&k_view_top, 255);
        C2D_DrawRectSolid(0, 0, 0.12f, 400, 240, rgba(0x06, 0x02, 0x0C, 0.62f));
        return;
    }
    /* her speech bubble (hidden while the outfit grid is open, like the original): upper right, tail towards her */
    draw_shop_scene(sc, &k_view_top, 234.0f, 56.0f, 150.0f);
}

/* ---------------------------------------------------------------- bottom screen */
static void draw_lock(float cx, float cy, float s, u32 col) {
    C2D_DrawCircleSolid(cx, cy - 4.0f * s, ZI, 4.2f * s, col);
    C2D_DrawCircleSolid(cx, cy - 4.0f * s, ZI + 0.001f, 2.4f * s, rgba(0x10, 0x0C, 0x14, 1.0f));
    rrect(cx - 6.0f * s, cy - 1.0f * s, 12.0f * s, 9.5f * s, 2.0f * s, col, ZI + 0.002f);
    C2D_DrawRectSolid(cx - 1.0f * s, cy + 2.0f * s, ZI + 0.003f, 2.0f * s, 3.5f * s, rgba(0x10, 0x0C, 0x14, 1.0f));
}

/* PlayerCash: "$<amount>" counting up to the new value over 1 s (expo out) and the rank title in a wave (amp 10, freq 5) under it. */
static void bottom_header(bb_screens_t* sc, const char* title, bool show_rank) {
    char buf[48];
    const float a = bb_ui_appear(0.0f, 0.3f), t = bb_ui_time();
    C2D_DrawRectangle(0, 0, ZP - 0.02f, 320, 32.0f, rgba(0, 0, 0, 0.55f), rgba(0, 0, 0, 0.55f), rgba(0, 0, 0, 0.0f), rgba(0, 0, 0, 0.0f));
    TS(BB_FACE_BOLD, 17, 12.0f - 24.0f * (1.0f - a), 6.0f, white(a), C2D_AlignLeft, title);
    const int money = (int)sc->save->player.money;
    if (money != sc->cash_to) {   /* update(): tween from the last shown amount */
        const float e0 = (t - sc->cash_t0) / 1.0f;
        sc->cash_from = e0 >= 1.0f || e0 < 0.0f ? sc->cash_to : sc->cash_from + (int)((float)(sc->cash_to - sc->cash_from) * (1.0f - powf(2.0f, -10.0f * e0)));
        sc->cash_to = money;
        sc->cash_t0 = t;
    }
    const float e = (t - sc->cash_t0) / 1.0f;
    const int shown = e >= 1.0f || e < 0.0f ? sc->cash_to : sc->cash_from + (int)((float)(sc->cash_to - sc->cash_from) * (1.0f - powf(2.0f, -10.0f * e)));
    snprintf(buf, sizeof(buf), "$%d", shown);
    T(BB_FACE_BOLD, 17, 310.0f, 4.0f, gold(a), C2D_AlignRight, buf);
    if (show_rank && bb_font_ready()) {
        const char* rk = bb_rank_title(bb_rank_from_exp(sc->save->player.exp));
        float x = 310.0f - bb_font_width(BB_FACE_BODY, 10, rk);
        for (int i = 0; rk[i]; i++) {
            char ch[2] = { rk[i], 0 };
            const float dy = bb_ui_reduce_motion() ? 0.0f : sinf(t * 5.0f + (float)i * 0.6f) * 1.5f;
            bb_font_draw(BB_FACE_BODY, 10, x, 24.0f + dy, gray(a), C2D_AlignLeft, ch, ZT);
            x += bb_font_width(BB_FACE_BODY, 10, ch);
        }
    }
}

static void bottom_hint(const char* s) {
    T(BB_FACE_BODY, 10, 160.0f, 226.0f, rgba(0xC8, 0xC8, 0xD8, 0.75f * bb_ui_appear(0.25f, 0.3f)), C2D_AlignCenter, s);
}

static void pill(float x, float y, float w, float h, bool sel, float a, float pulse) {
    rrect(x, y, w, h, h * 0.5f, sel ? white(a * (0.65f + 0.35f * pulse)) : rgba(0x6A, 0x6A, 0x76, a), ZP);
    rrect(x + 1.5f, y + 1.5f, w - 3.0f, h - 3.0f, h * 0.5f - 1.5f, rgba(0x06, 0x05, 0x0A, 0.92f * a), ZP + 0.004f);
}

static void draw_ui_bottom(bb_screens_t* sc);

void bb_screens_draw_bottom(bb_screens_t* sc, C2D_TextBuf tb) {
    g_tb = tb;
    if (!bb_screens_is_shop(sc)) { draw_ui_bottom(sc); return; }
    bb_player_t* p = &sc->save->player;
    int idx[SC_MAX_ROWS];
    char buf[96];
    const float pulse = 0.5f + 0.5f * sinf(bb_ui_time() * 5.0f);
    if (sc->node == SCN_SHOP && sc->booping) {   /* Boop: Jasmine up close on the touch screen; her nose is the honk button */
        draw_shop_scene(sc, &k_view_boop, 196.0f, 34.0f, 116.0f);
        /* Jasmine.tscn Honk (Root -62,-187 .. 43,-115 around her centred 919x1148 frame): 115.5..146 x 112.4..133.3 of the port frame,
         * padded for a fingertip */
        const shop_view_t* v = &k_view_boop;
        const float nx0 = (JAS_X0 + 115.5f * JAS_SCALE - v->cx) * v->zoom + v->ox, ny0 = (JAS_Y0 + 112.4f * JAS_SCALE - v->cy) * v->zoom + v->oy;
        const float nw = 30.5f * JAS_SCALE * v->zoom, nh = 20.9f * JAS_SCALE * v->zoom;
        bb_ui_zone_add(BB_ZONE_HANDSFREE, nx0 - 8.0f, ny0 - 8.0f, nw + 16.0f, nh + 16.0f);
        const float a = bb_ui_appear(0.0f, 0.3f);
        rrect(6.0f, 214.0f, 120.0f, 20.0f, 10.0f, rgba(0, 0, 0, 0.6f * a), ZP + 0.05f);
        T(BB_FACE_BOLD, 12, 70.0f, 217.0f, white(0.55f + 0.45f * pulse), C2D_AlignCenter, "Boop her nose!");
        back_button(296.0f, 222.0f, a);
        bb_ui_zone_add(BB_ZONE_BACK, 278.0f, 204.0f, 36.0f, 36.0f);
        return;
    }
    /* the same room, dimmed (the original darkens it to 0.25 whenever a shop menu is open: ShopBackground.set_brightness) */
    bb_art_background_crop(BB_BG_SHOP, 84, 40.0f, 320.0f, 0.10f);
    C2D_DrawRectSolid(0, 0, 0.12f, 320, 240, rgba(0x06, 0x02, 0x0C, 0.38f));

    if (sc->node == SCN_SHOP) {
        /* shop_options.tscn: ShopOptionsButton - #090909 rounded 8, 480 x 96 (1080p), the label left in Encode Sans Condensed Bold 50,
         * the icon at the right edge; the focused one grows to 582 wide (expo 0.5 s) and its icon goes 0.3 -> 1.0 alpha; UiButton
         * modulate dim gray -> white smoke. Buy, Talk, Outfit (Gachapon is not in the port), then the port's Boop. */
        static const char* const opts[4] = { "Buy", "Talk", "Outfit", "Boop" };
        (void)idx;
        const float K = 240.0f / 1080.0f * 1.45f;   /* the buttons at a readable size for the touch screen */
        for (int i = 0; i < 4; i++) {
            const float a = bb_ui_appear(0.05f + 0.06f * (float)i, 0.35f);
            const bool sel = i == sc->cursor[SCN_SHOP];
            if (sc->opt_w[i] <= 0.0f || bb_ui_enter_time() < 0.03f) sc->opt_w[i] = sel ? 582.0f : 480.0f;
            bb_ui_smooth(&sc->opt_w[i], sel ? 582.0f : 480.0f, 9.0f);
            const float w = sc->opt_w[i] * K, h = 96.0f * K;
            const float x = 26.0f - 40.0f * (1.0f - a), y = 30.0f + (float)i * (h + 9.0f);
            const float m = sel ? 0.96f : 0.41f;   /* WHITE_SMOKE / DIM_GRAY */
            rrect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, 8.0f * K * 2.0f + 2.0f, rgba(0xF4, 0xF4, 0xF8, (sel ? 0.95f : 0.7f) * a), ZP - 0.002f);   /* 2 px white outline */
            rrect(x, y, w, h, 8.0f * K * 2.0f, rgba(0x09, 0x09, 0x09, 0.96f * a), ZP);
            bb_ui_zone_add(BB_ZONE_LEVEL + i, x, y, w, h);
            if (bb_font_ready()) bb_font_draw_smallcaps(BB_FACE_BOLD, 21, x + 12.0f, y + (h - 24.0f) * 0.5f, rgba((u8)(255.0f * m), (u8)(255.0f * m), (u8)(255.0f * m), a), C2D_AlignLeft, opts[i], ZT);
            const float ia = (sel ? 1.0f : 0.3f) * a * m;
            const float icx = x + w - 50.0f * K - 2.0f, icy = y + h * 0.5f;
            {   /* the option's white icon (shop.tscn ShopOptionButton button_icon; Boop: the port's own) */
                static C2D_SpriteSheet icons = NULL;
                if (!icons) icons = C2D_SpriteSheetLoad("romfs:/ui/shop_opt_icons.t3x");
                if (icons) {
                    C2D_Image img = C2D_SpriteSheetGetImage(icons, 0);
                    Tex3DS_SubTexture st;
                    set_sub(&st, (float)i * 128.0f, 0.0f, 128.0f, 128.0f, (float)img.tex->width, (float)img.tex->height);
                    img.subtex = &st;
                    C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
                    C2D_ImageTint t;
                    C2D_PlainImageTint(&t, C2D_Color32(255, 255, 255, (u8)(255.0f * ia)), 0.0f);
                    const float sz = 64.0f * K;
                    C2D_DrawParams dp = { { icx - sz * 0.5f, icy - sz * 0.5f, sz, sz }, { 0, 0 }, ZI, 0.0f };
                    C2D_DrawImage(img, &dp, &t);
                }
            }
        }
        back_button(16.0f, 224.0f, bb_ui_appear(0.2f, 0.3f) * 0.85f);
        bb_ui_zone_add(BB_ZONE_BACK, 0, 206.0f, 34.0f, 34.0f);
        return;
    } else if (sc->node == SCN_SHOP_LIST) {
        const int n = shop_count_cat(sc, k_cat_code[sc->shop_cat], idx);
        bottom_header(sc, "BUY", true);   /* PlayerCash is part of the BuyMenu */
        for (int c = 0; c < 4; c++) {   /* category tabs, L / R switches (shop_categories.gd) */
            const float a = bb_ui_appear(0.03f * (float)c, 0.3f);
            const float cx = 41.0f + (float)c * 79.0f;
            const bool on = c == sc->shop_cat;
            bb_art_category(c, cx, 42.0f, on ? 24.0f : 18.0f, on ? 0xFFFFFFFFu : C2D_Color32(0x78, 0x78, 0x80, (u8)(255.0f * a)), ZI);
            T(BB_FACE_BOLD, 10, cx, 55.0f, on ? white(a) : gray(a * 0.9f), C2D_AlignCenter, k_cat_name[c]);
            if (on) C2D_DrawRectSolid(cx - 32.0f, 69.0f, ZP + 0.02f, 64.0f, 2.0f, shop_orange(a));
            bb_ui_zone_add(BB_ZONE_ACT + c, cx - 39.0f, 30.0f, 78.0f, 40.0f);
        }
        T(BB_FACE_BOLD, 10, 8.0f, 40.0f, gray(0.9f), C2D_AlignLeft, "L");
        T(BB_FACE_BOLD, 10, 312.0f, 40.0f, gray(0.9f), C2D_AlignRight, "R");
        const float rh = 31.0f, y0 = 74.0f;
        const int cur = sc->cursor[SCN_SHOP_LIST];
        int first = cur - 1;
        if (first > n - 4) first = n - 4;
        if (first < 0) first = 0;
        const float since = bb_ui_time() - sc->list_t0;
        for (int r = 0; r < 4 && first + r < n; r++) {
            const int i = first + r;
            const bb_shop_item_t* it = &sc->ex->shop[idx[i]];
            const bool owned = bb_player_has_item(p, it->item);
            const bool locked = p->exp < it->req_exp;
            const bool sel = i == cur;
            const float a = bb_ui_ease_out((since - 0.04f * (float)r) / 0.3f);
            const float x = 6.0f - 24.0f * (1.0f - a), y = y0 + (float)r * rh;
            rrect(x, y, 308.0f, rh - 3.0f, 4.0f, rgba(0x0F, 0x0F, 0x0F, (sel ? 0.96f : 0.8f) * a), ZP);   /* ShopItem panel 0.0588, radius 5 */
            bb_ui_zone_add(BB_ZONE_LEVEL + i, 6.0f, y, 308.0f, rh - 3.0f);
            if (sel) C2D_DrawRectSolid(x, y + 3.0f, ZP + 0.02f, 3.0f, rh - 9.0f, shop_orange(a));
            if (locked) {   /* ShopItem: rank-locked items hide name, price and description behind a lock and the required rank */
                draw_lock(x + 22.0f, y + (rh - 3.0f) * 0.5f, 1.1f, rgba(0xA0, 0xA0, 0xAC, a));
                {   /* "Required Rank: [b][color=fcac21]<title>" */
                    const float lw = bb_font_ready() ? bb_font_width(BB_FACE_BOLD, 12, "Required Rank: ") : 90.0f;
                    T(BB_FACE_BOLD, 12, x + 42.0f, y + 6.0f, white(a), C2D_AlignLeft, "Required Rank: ");
                    T(BB_FACE_BOLD, 12, x + 42.0f + lw, y + 6.0f, rgba(0xFC, 0xAC, 0x21, a), C2D_AlignLeft, bb_rank_title(bb_rank_from_exp(it->req_exp)));
                }
                continue;
            }
            bb_art_shop_icon(it->item, sc->shop_cat, x + 22.0f, y + (rh - 3.0f) * 0.5f, 22.0f,
                             owned ? C2D_Color32(0x68, 0x68, 0x70, (u8)(255.0f * a)) : C2D_Color32(255, 255, 255, (u8)(255.0f * a)), ZI);
            const u32 nc = owned ? rgba(0x99, 0x3A, 0x00, a) : rgba(0xFF, 0x60, 0x00, a);   /* ItemNameLabel (1, 0.375, 0) */
            TS(BB_FACE_BOLD, 14, x + 42.0f, y + 5.0f, nc, C2D_AlignLeft, it->name);
            if (owned) {
                T(BB_FACE_HEAD, 20, x + 300.0f, y + 1.0f, rgba(0xFC, 0x21, 0x21, a), C2D_AlignRight, "SOLD OUT");   /* (0.99, 0.13, 0.13) */
            } else {
                snprintf(buf, sizeof(buf), "$%d", it->price);
                T(BB_FACE_BOLD, 14, x + 300.0f, y + 5.0f, p->money >= it->price ? rgba(0xFC, 0xAC, 0x21, a) : rgba(0xE0, 0x50, 0x50, a), C2D_AlignRight, buf);   /* PriceLabel (0.99, 0.67, 0.13) */
            }
        }
        if (n > 4) {
            const float th = 4.0f * rh * 4.0f / (float)n, ty = y0 + (4.0f * rh - th) * (float)first / (float)(n - 4);
            C2D_DrawRectSolid(317.0f, ty, ZP + 0.03f, 2.0f, th, rgba(1, 1, 1, 0.5f));
        }
        if (n > 0) {   /* detail strip for the highlighted item */
            const bb_shop_item_t* it = &sc->ex->shop[idx[cur]];
            const bool owned = bb_player_has_item(p, it->item), locked = p->exp < it->req_exp;
            rrect(6.0f, 202.0f, 308.0f, 35.0f, 5.0f, rgba(0x05, 0x04, 0x0A, 0.84f), ZP);
            if (locked) {
                snprintf(buf, sizeof(buf), "Reach the rank %s to unlock this item.", bb_rank_title(bb_rank_from_exp(it->req_exp)));
                T(BB_FACE_BODY, 10, 14.0f, 213.0f, gray(1.0f), C2D_AlignLeft, buf);
            } else {
                draw_wrapped(BB_FACE_BODY, 10, 14.0f, 205.0f, 11.0f, 218.0f, rgba(0xD0, 0xD0, 0xDA, 1.0f), C2D_AlignLeft, it->desc, 3);
                if (owned) T(BB_FACE_BOLD, 10, 306.0f, 213.0f, rgba(0xE0, 0x50, 0x50, 1.0f), C2D_AlignRight, "ALREADY BOUGHT");
                else if (p->money < it->price) T(BB_FACE_BOLD, 10, 306.0f, 213.0f, rgba(0xE0, 0x50, 0x50, 1.0f), C2D_AlignRight, "NOT ENOUGH");
                else T(BB_FACE_BOLD, 12, 306.0f, 212.0f, white(0.55f + 0.45f * pulse), C2D_AlignRight, "A  BUY");
            }
        }
    } else if (sc->node == SCN_SHOP_TALK) {
        const int n = talk_count_set(sc, idx);
        bottom_header(sc, "TALK", false);
        for (int i = 0; i < n && i < 6; i++) {
            const float a = bb_ui_appear(0.08f + 0.05f * (float)i, 0.3f);
            const bool sel = i == sc->cursor[SCN_SHOP_TALK];
            const float x = 12.0f - 30.0f * (1.0f - a), y = 40.0f + (float)i * 31.0f;
            pill(x, y, 296.0f, 27.0f, sel, a, pulse);
            bb_ui_zone_add(BB_ZONE_LEVEL + i, 12.0f, y, 296.0f, 27.0f);
            T(BB_FACE_BOLD, 12, x + 148.0f, y + 6.0f, sel ? white(a) : gray(a), C2D_AlignCenter, sc->ex->talk[idx[i]].input);
        }
        bottom_hint("D-Pad: move     A: say it     B: done talking");
    } else {   /* outfits: 3 x 2 slots in the original order; locked ones show a lock and ??? (shop_outfit_icon.gd) */
        int rows[6];
        const int n = outfit_rows(sc, rows);
        {
            char hd[24], up[24];
            snprintf(up, sizeof(up), "%s", k_outfit_page[sc->outfit_page % OUTFIT_PAGES]);
            for (char* c = up; *c; c++) *c = (char)toupper((unsigned char)*c);
            snprintf(hd, sizeof(hd), "%s", up);
            bottom_header(sc, hd, false);
            const float arrow = 2.0f * sinf(bb_ui_time() * 6.0f);   /* the original's arrow buttons (L / R) */
            T(BB_FACE_BOLD, 17, 148.0f - arrow, 4.0f, white(0.85f), C2D_AlignCenter, "<");
            T(BB_FACE_BOLD, 17, 196.0f + arrow, 4.0f, white(0.85f), C2D_AlignCenter, ">");
            bb_ui_zone_add(BB_ZONE_PREV, 132.0f, 0, 32.0f, 30.0f);
            bb_ui_zone_add(BB_ZONE_NEXT, 180.0f, 0, 32.0f, 30.0f);
        }
        for (int i = 0; i < n; i++) {
            const float a = bb_ui_appear(0.05f + 0.04f * (float)i, 0.3f);
            const bool sel = i == sc->cursor[SCN_SHOP_OUTFIT];
            const bool owned = rows[i] == 0 || bb_player_has_item(p, rows[i]);
            const float x = 17.0f + (float)(i % 3) * 102.0f, y = 42.0f + (float)(i / 3) * 90.0f - 18.0f * (1.0f - a);
            const float grow = sel ? 2.0f + 0.8f * pulse : 0.0f;
            frame(x - grow, y - grow, 90.0f + 2.0f * grow, 82.0f + 2.0f * grow, 4.0f, sel ? white(a) : rgba(0x5A, 0x5A, 0x68, a), rgba(0x08, 0x06, 0x0C, 0.88f * a), ZP);
            bb_ui_zone_add(BB_ZONE_LEVEL + i, x, y, 90.0f, 82.0f);
            const char* nm = rows[i] == 1106 ? "Nurse" : "Vest";   /* OUTFIT_NURSE */
            for (int k = 0; k < sc->ex->shop_count; k++) if (rows[i] && sc->ex->shop[k].item == rows[i]) nm = sc->ex->shop[k].name;
            if (owned && rows[i] == 1106) {   /* seasonal outfits are not shop items: their own icon (outfit_icon_<item>.t3x) */
                static C2D_SpriteSheet nurse = NULL;
                if (!nurse) nurse = bb_extras_load_sheet("outfit_icon_1106.t3x");
                if (nurse) {
                    C2D_Image img = C2D_SpriteSheetGetImage(nurse, 0);
                    C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
                    C2D_ImageTint t;
                    C2D_PlainImageTint(&t, C2D_Color32(255, 255, 255, (u8)(255.0f * a)), 0.0f);
                    C2D_DrawParams dp = { { x + 45.0f - 22.0f, y + 34.0f - 22.0f, 44.0f, 44.0f }, { 0, 0 }, ZI, 0.0f };
                    C2D_DrawImage(img, &dp, &t);
                }
            } else if (owned) bb_art_shop_icon(10000 + (rows[i] ? rows[i] : 1100), 3, x + 45.0f, y + 34.0f, 44.0f, C2D_Color32(255, 255, 255, (u8)(255.0f * a)), ZI);
            else draw_lock(x + 45.0f, y + 34.0f, 1.7f, rgba(0xA0, 0xA0, 0xAC, a * 0.7f));
            snprintf(buf, sizeof(buf), "%.18s", owned ? nm : "???");
            T(BB_FACE_BODY, 10, x + 45.0f, y + 62.0f, sel ? white(a) : gray(a), C2D_AlignCenter, buf);
            if (owned && p->outfit == rows[i]) T(BB_FACE_BOLD, 10, x + 45.0f, y + 4.0f, gold(a), C2D_AlignCenter, "WEARING");
        }
        bottom_hint("D-Pad: move     L/R: page     A: wear     B: back");
    }
    back_button(16.0f, 224.0f, bb_ui_appear(0.2f, 0.3f) * 0.85f);
    bb_ui_zone_add(BB_ZONE_BACK, 0, 206.0f, 34.0f, 34.0f);
}

/* ---------------------------------------------------------------- EXTRAS (bottom): extras.tscn
 * The room (EnvBackground ui_extras_dark) with the monitor (its lit Polygon2D screen + EnvMonitor), cropped to keep the whole monitor
 * (tools/build_extras_menu.py), the additive Glow on the screen ("glow": alpha 0.259 -> 0.384 -> 0.259 over 0.8 s), ExtrasText hovering
 * 15 px over 2 s, the slanted black ColorRect with the Sawtooth along its edge (rotation 0.1427), and the six ExtrasButtons: icon,
 * Encode Sans SC condensed label with its shadow, UiButton dim gray / white smoke, the selected one's white gradient bar
 * (0 -> 0.144 at 8.9 % -> 0) and its label sliding 94 -> 134 (0.3 s, back ease). */
static C2D_SpriteSheet g_xm_bg, g_xm_text, g_xm_icons;

static void xm_load(void) {
    if (!g_xm_bg) g_xm_bg = bb_extras_load_sheet("xm_bg.t3x");
    if (!g_xm_text) g_xm_text = bb_extras_load_sheet("xm_text.t3x");
    if (!g_xm_icons) g_xm_icons = bb_extras_load_sheet("xm_icons.t3x");
}

static void xm_free(void) {
    free_sheet(&g_xm_bg);
    free_sheet(&g_xm_text);
    free_sheet(&g_xm_icons);
}

static void xm_cell(int cell, float cx, float cy, float w, float h, float angle, u32 tint, float z) {
    if (!g_xm_icons) return;
    C2D_Image img = C2D_SpriteSheetGetImage(g_xm_icons, 0);
    Tex3DS_SubTexture st;
    set_sub(&st, (float)cell * 64.0f, 0.0f, 64.0f, 64.0f, (float)img.tex->width, (float)img.tex->height);
    img.subtex = &st;
    C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, tint, 0.0f);
    C2D_DrawParams dp = { { cx, cy, w, h }, { w * 0.5f, h * 0.5f }, z, angle };
    C2D_DrawImage(img, &dp, &t);
}

static void draw_extras(bb_screens_t* sc) {
    static const char* const opts[6] = { "Music", "Gallery", "Theater", "Characters", "Scene Viewer", "Collection" };
    static float lab_x[6], mod_v[6];
    xm_load();
    const float now = bb_ui_time();
    const float K = 240.0f / 1080.0f;
    /* the room and the monitor */
    if (g_xm_bg) {
        C2D_Image img = C2D_SpriteSheetGetImage(g_xm_bg, 0);
        Tex3DS_SubTexture st;
        set_sub(&st, 0.0f, 0.0f, 320.0f, 240.0f, (float)img.tex->width, (float)img.tex->height);
        img.subtex = &st;
        C2D_DrawImageAt(img, 0.0f, 0.0f, 0.10f, NULL, 1.0f, 1.0f);
    } else {
        bb_art_background_crop(BB_BG_EXTRAS, 255, 40.0f, 320.0f, 0.10f);
    }
    {   /* the Glow: a radial white gradient on the screen, pulsing */
        const float ph = fmodf(now, 0.8f) / 0.8f, e = 0.5f - 0.5f * cosf(ph * 6.2831853f);
        const float ga = 0.259f + (0.384f - 0.259f) * e;
        const float cx = (1472.0f - 480.0f) * K, cy = 634.0f * K, rx = 122.0f, ry = 79.0f;
        const u32 c0 = rgba(0xFF, 0xFF, 0xFF, ga), c1 = rgba(0xFF, 0xFF, 0xFF, 0.0f);
        for (int k = 0; k < 32; k++) {
            const float a0 = (float)k * 0.19635f, a1 = a0 + 0.19635f;
            C2D_DrawTriangle(cx, cy, c0, cx + cosf(a0) * rx, cy + sinf(a0) * ry, c1, cx + cosf(a1) * rx, cy + sinf(a1) * ry, c1, 0.12f);
        }
    }
    if (g_xm_text) {   /* ExtrasText "Hover": 0 -> -15 -> 0 over 2 s */
        const float ph = fmodf(now, 2.0f) / 2.0f, e = 0.5f - 0.5f * cosf(ph * 6.2831853f);
        C2D_Image img = C2D_SpriteSheetGetImage(g_xm_text, 0);
        C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
        const float w = (float)img.subtex->width / 1.5f, hh = (float)img.subtex->height / 1.5f;
        C2D_DrawParams dp = { { (1388.0f - 480.0f) * K, 56.0f * K - 15.0f * K * e, w, hh }, { 0, 0 }, 0.14f, 0.0f };
        C2D_DrawImage(img, &dp, NULL);
    }
    /* the slanted black panel and its sawtooth edge */
    const float pa = bb_ui_appear(0.0f, 0.4f);
    const float sl = -40.0f * (1.0f - pa);
    const float ex0 = 64.0f + sl, ex1 = 30.0f + sl;   /* the edge: x at the top and at the bottom */
    const u32 blk = C2D_Color32(0, 0, 0, 0xFF);
    C2D_DrawTriangle(-2.0f, 0.0f, blk, ex0, 0.0f, blk, -2.0f, 240.0f, blk, ZP - 0.02f);
    C2D_DrawTriangle(ex0, 0.0f, blk, ex1, 240.0f, blk, -2.0f, 240.0f, blk, ZP - 0.02f);
    {
        const float ang = 0.142687f, tile = 14.0f;
        for (int k = 0; k < 19; k++) {
            const float t = ((float)k + 0.5f) * tile / 240.0f;
            const float cx = ex0 + (ex1 - ex0) * t + tile * 0.5f, cy = (float)k * tile + tile * 0.5f;
            xm_cell(6, cx, cy, tile, tile, ang, C2D_Color32(255, 255, 255, 255), ZP - 0.015f);
        }
    }
    /* the buttons */
    for (int i = 0; i < 6; i++) {
        const float a = bb_ui_appear(0.06f + 0.05f * (float)i, 0.35f);
        const bool sel = i == sc->cursor[SCN_EXTRAS];
        const float y = 22.0f + (float)i * 29.0f, h = 26.0f;
        if (bb_ui_enter_time() < 0.03f) { lab_x[i] = sel ? 134.0f : 94.0f; mod_v[i] = sel ? 0.96f : 0.41f; }
        bb_ui_smooth(&lab_x[i], sel ? 134.0f : 94.0f, 16.0f);
        bb_ui_smooth(&mod_v[i], sel ? 0.96f : 0.41f, 12.0f);
        if (sel) {   /* Gradient (visible while focused): additive white, 0 -> 0.144 at 8.9 % -> 0 */
            const float w = 168.0f, xk = w * 0.089f;
            const u32 g0 = rgba(0xFF, 0xFF, 0xFF, 0.0f), g1 = rgba(0xFF, 0xFF, 0xFF, 0.144f * a * 1.4f);
            C2D_DrawRectangle(0, y, ZP, xk, h, g0, g1, g0, g1);
            C2D_DrawRectangle(xk, y, ZP, w - xk, h, g1, g0, g1, g0);
        }
        const u8 v = (u8)(255.0f * mod_v[i]);   /* the icon follows the button's modulate */
        xm_cell(i, 16.0f, y + h * 0.5f, 22.0f, 22.0f, 0.0f, C2D_Color32(v, v, v, (u8)(255.0f * a)), ZI);
        const float lx = 30.0f + (lab_x[i] - 94.0f) * K * 1.1f;
        if (bb_font_ready()) {   /* LabelSettings: shadow (0, 2) at 0.275 */
            bb_font_draw_smallcaps(BB_FACE_BOLD, 17, lx, y + 4.5f, rgba(0, 0, 0, 0.275f * a), C2D_AlignLeft, opts[i], ZT - 0.01f);
            bb_font_draw_smallcaps(BB_FACE_BOLD, 17, lx, y + 3.5f, rgba((u8)(255.0f * mod_v[i]), (u8)(255.0f * mod_v[i]), (u8)(255.0f * mod_v[i]), a), C2D_AlignLeft, opts[i], ZT);
        }
        bb_ui_zone_add(BB_ZONE_LEVEL + i, 0, y, 168.0f, h + 2.0f);
    }
    back_button(22.0f, 216.0f, bb_ui_appear(0.3f, 0.3f));
    bb_ui_zone_add(BB_ZONE_BACK, 4.0f, 198.0f, 36.0f, 38.0f);
}

/* ---------------------------------------------------------------- THEATER (theater.gd): the first panel of the highlighted scene in a film frame */
/* Load the highlighted reel's first panel on demand; returns the reel index (or -1 without reels). */
static int theater_ensure(bb_screens_t* sc, bool* open, float* shown_t) {
    static float prev_t = -10.0f;
    const int n = sc->reel_stages * 2;
    if (n == 0) return -1;
    const int cur = sc->cursor[SCN_THEATER] < n ? sc->cursor[SCN_THEATER] : 0;
    *open = bb_screens_reel_open(sc, cur);
    if (sc->detail_key != 3000 + cur * 2 + (*open ? 1 : 0)) {
        free_sheet(&sc->detail);
        if (*open) {
            char path[96];
            snprintf(path, sizeof(path), "romfs:/stages/%s/cutscene/%s_0.t3x", sc->reel_id[cur / 2], (cur & 1) ? "post" : "pre");
            sc->detail = C2D_SpriteSheetLoad(path);
        }
        sc->detail_key = 3000 + cur * 2 + (*open ? 1 : 0);
        prev_t = bb_ui_time();
    }
    *shown_t = prev_t;
    return cur;
}

static void film_frame(bb_screens_t* sc, bool open, int cur, float shown_t, float fx, float fy, float fw, float fh, float fa) {
    const float hole = fw / 14.0f;
    C2D_DrawRectSolid(fx - fw * 0.035f, fy - fh * 0.11f, ZP, fw * 1.07f, fh * 1.22f, rgba(0x06, 0x06, 0x08, 0.96f * fa));
    for (int k = 0; k < 15; k++) {   /* sprocket holes */
        const float hx = fx - 2.0f + (float)k * hole * 1.0f;
        C2D_DrawRectSolid(hx, fy - fh * 0.08f, ZP + 0.01f, hole * 0.45f, fh * 0.04f, rgba(0x3A, 0x3A, 0x44, fa));
        C2D_DrawRectSolid(hx, fy + fh + fh * 0.04f, ZP + 0.01f, hole * 0.45f, fh * 0.04f, rgba(0x3A, 0x3A, 0x44, fa));
    }
    C2D_DrawRectSolid(fx, fy, ZP + 0.01f, fw, fh, rgba(0x12, 0x12, 0x16, fa));
    if (open && sc->detail) {   /* the panel art is 400x225 at the top left of its 512x256 texture: that area fills the frame */
        C2D_Image img = C2D_SpriteSheetGetImage(sc->detail, 0);
        Tex3DS_SubTexture st;
        set_sub(&st, 0.0f, 0.0f, 400.0f, 225.0f, (float)img.tex->width, (float)img.tex->height);
        img.subtex = &st;
        C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
        C2D_DrawParams dp = { { fx, fy, fw, fh }, { 0, 0 }, ZI, 0.0f };
        C2D_DrawImage(img, &dp, NULL);
        const float e = bb_ui_reduce_motion() ? 1.0f : bb_ui_ease_out((bb_ui_time() - shown_t) / 0.3f);
        if (e < 1.0f) C2D_DrawRectSolid(fx, fy, ZI + 0.01f, fw, fh, rgba(0, 0, 0, 1.0f - e));
    } else if (!open) {
        draw_lock(fx + fw * 0.5f, fy + fh * 0.5f - fh * 0.08f, fh / 60.0f, rgba(0x80, 0x80, 0x8C, fa));
        T(BB_FACE_BOLD, 12, fx + fw * 0.5f, fy + fh * 0.5f + fh * 0.12f, gray(fa), C2D_AlignCenter, (cur & 1) ? "Pass this level to unlock" : "Unlock this level first");
    }
}

/* TOP: the same picture, bigger and alone, in the film frame over the Extras background */
static void draw_theater_top(bb_screens_t* sc) {
    bool open = false;
    float shown = 0.0f;
    bb_art_background(BB_BG_EXTRAS, 70, 0.10f);
    const int cur = theater_ensure(sc, &open, &shown);
    if (cur < 0) return;
    film_frame(sc, open, cur, shown, 56.0f, 33.0f, 288.0f, 162.0f, 1.0f);
}

/* BOTTOM: a small frame, the scene's name, and the reel list (before / after per level) */
static void draw_theater_bottom(bb_screens_t* sc) {
    const int n = sc->reel_stages * 2;
    char buf[96], up[32];
    bb_art_background_crop(BB_BG_EXTRAS, 70, 40.0f, 320.0f, 0.10f);
    const float a = bb_ui_appear(0.0f, 0.4f);
    T(BB_FACE_HEAD, 24, 12.0f - 20.0f * (1.0f - a), 2.0f, white(a), C2D_AlignLeft, "THEATER");
    back_button(22.0f, 216.0f, a);
    bb_ui_zone_add(BB_ZONE_BACK, 4.0f, 198.0f, 36.0f, 38.0f);
    bool open = false;
    float shown = 0.0f;
    const int cur = theater_ensure(sc, &open, &shown);
    if (cur < 0) return;
    const int st = cur / 2;
    const float fa = bb_ui_appear(0.05f, 0.4f);
    film_frame(sc, open, cur, shown, 14.0f, 48.0f, 140.0f, 79.0f, fa);
    u8 r, g, b;
    name_color(sc->reel_char[st], &r, &g, &b);
    snprintf(up, sizeof(up), "%s", sc->reel_char[st]);
    for (char* c = up; *c; c++) *c = (char)toupper((unsigned char)*c);
    T(BB_FACE_HEAD, 20, 12.0f, 142.0f, rgba(r, g, b, fa), C2D_AlignLeft, up);
    snprintf(buf, sizeof(buf), "%s - %.30s", (cur & 1) ? "After" : "Before", sc->reel_title[st]);
    T(BB_FACE_BOLD, 12, 12.0f, 168.0f, white(fa), C2D_AlignLeft, buf);

    /* reel list: six rows, scrolling with the cursor (and a stylus drag on top of it) */
    static float scroll = 0.0f;
    const int vis = 6;
    float target = (float)(cur - vis / 2 + 1) + g_th_off / 30.0f;
    if (target > (float)(n - vis)) target = (float)(n - vis);
    if (target < 0.0f) target = 0.0f;
    {   /* keep the drag inside the list */
        const float base = (float)(cur - vis / 2 + 1);
        g_th_off = (target - base) * 30.0f;
    }
    if (bb_ui_enter_time() < 0.03f) scroll = target;
    bb_ui_smooth(&scroll, target, g_thd.drag ? 30.0f : 14.0f);
    const float lx = 176.0f, ly = 36.0f, rh = 30.0f;
    for (int i = 0; i < n; i++) {
        const float y = ly + ((float)i - scroll) * rh;
        if (y < ly - rh * 0.5f || y > ly + rh * ((float)vis - 0.5f)) continue;
        const float ra = bb_ui_appear(0.08f + 0.03f * (float)(i % vis), 0.3f);
        const bool sel = i == cur, ok = bb_screens_reel_open(sc, i);
        const float x = lx + 24.0f * (1.0f - ra);
        if (sel) {
            C2D_DrawRectSolid(x - 6.0f, y - 2.0f, ZP + 0.02f, 320.0f - x + 6.0f, rh - 2.0f, rgba(0xFF, 0xFF, 0xFF, 0.10f * ra));
            C2D_DrawRectSolid(x - 6.0f, y + 2.0f, ZP + 0.03f, 3.0f, rh - 10.0f, shop_orange(ra));
        }
        const u8 v = ok ? (sel ? 255 : 170) : 60;
        bb_art_thumb(sc->reel_id[i / 2], x + 10.0f, y + 12.0f, 20.0f, C2D_Color32(v, v, v, (u8)(255.0f * ra)), ZI);
        if (!ok) draw_lock(x + 10.0f, y + 13.0f, 0.8f, rgba(0xB0, 0xB0, 0xBC, ra));
        T(BB_FACE_BOLD, 12, x + 26.0f, y, sel ? white(ra) : (ok ? gray(ra) : rgba(0x58, 0x58, 0x62, ra)), C2D_AlignLeft, sc->reel_char[i / 2]);
        T(BB_FACE_BODY, 10, x + 26.0f, y + 13.0f, ok ? gray(0.9f * ra) : rgba(0x50, 0x50, 0x5A, ra), C2D_AlignLeft, (i & 1) ? "After" : "Before");
        bb_ui_zone_add(BB_ZONE_LEVEL + i, lx - 8.0f, y - 2.0f, 320.0f - lx + 8.0f, rh - 2.0f);
    }
}

/* ---------------------------------------------------------------- ACHIEVEMENTS (bottom): the drawer over the dimmed main menu */
static void draw_achievements(bb_screens_t* sc) {
    bb_player_t* p = &sc->save->player;
    const bb_ach_def_t* d = bb_ach_defs();
    char buf[96];
    bb_ui_main_menu_bottom(0, 255, false);
    C2D_DrawRectSolid(0, 0, 0.30f, 320, 240, rgba(0, 0, 0, 0.57f));   /* the menu behind, dimmed (under the drawer) */
    /* the drawer comes out of the right side over the dimmed menu, as it did on the top screen (user preference) */
    float pa = bb_ui_appear(0.0f, 0.45f);
    float px = 78.0f + 242.0f * (1.0f - pa);
    torn_panel(px, 340.0f, false, ZP, C2D_Color32(0, 0, 0, 0xFF));
    int got = 0;
    for (int i = 0; i < BB_ACH_COUNT; i++) if (bb_player_has_ach(p, d[i].id)) got++;
    T(BB_FACE_HAND, 26, px + 14.0f, 4.0f, white(pa), C2D_AlignLeft, "Achievements");
    snprintf(buf, sizeof(buf), "%d / %d", got, BB_ACH_COUNT);
    T(BB_FACE_BOLD, 14, px + 232.0f, 10.0f, white(pa), C2D_AlignRight, buf);
    const float tile = 36.0f, gap = 7.0f, gx = px + 16.0f, gy = 40.0f;
    const int rows = (BB_ACH_COUNT + ACH_COLS - 1) / ACH_COLS, vis = 4;
    static float scroll = 0.0f;
    float target = (float)(sc->cursor[SCN_ACH] / ACH_COLS - 1);
    if (target > (float)(rows - vis)) target = (float)(rows - vis);
    if (target < 0.0f) target = 0.0f;
    if (bb_ui_enter_time() < 0.03f) scroll = target;
    bb_ui_smooth(&scroll, target, 14.0f);
    for (int i = 0; i < BB_ACH_COUNT; i++) {
        bool unlocked = bb_player_has_ach(p, d[i].id);
        bool sel = i == sc->cursor[SCN_ACH];
        float a = bb_ui_appear(0.12f + 0.02f * (float)i, 0.3f);
        float x = gx + (float)(i % ACH_COLS) * (tile + gap), y = gy + ((float)(i / ACH_COLS) - scroll) * (tile + gap);
        if (y < gy - tile * 0.5f || y > gy + (tile + gap) * ((float)vis - 0.5f)) continue;
        u32 border = sel ? white(a) : (unlocked ? rgba(0xFF, 0xA1, 0x1D, a) : rgba(0x6C, 0x6C, 0x76, a));
        float grow = sel ? 2.0f + 0.8f * sinf(bb_ui_time() * 6.0f) : 0.0f;
        frame(x - grow, y - grow, tile + 2.0f * grow, tile + 2.0f * grow, 4.0f, border, rgba(0x14, 0x14, 0x18, a), ZP + 0.02f);
        u32 tint = unlocked ? C2D_Color32(255, 255, 255, (u8)(255.0f * a)) : C2D_Color32(0x48, 0x48, 0x50, (u8)(255.0f * a));
        bb_art_ach(d[i].id, x + tile * 0.5f, y + tile * 0.5f, tile - 8.0f, tint, ZI);
        bb_ui_zone_add(BB_ZONE_LEVEL + i, x - 3.0f, y - 3.0f, tile + 6.0f, tile + 6.0f);
    }
    {   /* tooltip for the selected tile: white rounded box, black border (footage 0:06 / 0:08) */
        int i = sc->cursor[SCN_ACH];
        const bb_ach_def_t* cur = &d[i];
        bool unlocked = bb_player_has_ach(p, cur->id);
        float x = gx + (float)(i % ACH_COLS) * (tile + gap), y = gy + ((float)(i / ACH_COLS) - scroll) * (tile + gap);
        float tw = 170.0f, th = 40.0f;
        float tx = x + tile * 0.5f - tw * 0.5f;
        if (tx < px + 6.0f) tx = px + 6.0f;
        if (tx + tw > 316.0f) tx = 316.0f - tw;
        float ty = y + tile + 5.0f;
        if (ty + th > 236.0f) ty = y - th - 5.0f;
        float ta = bb_ui_appear(0.25f, 0.25f);
        frame(tx, ty, tw, th, 4.0f, rgba(0, 0, 0, ta), rgba(0xF8, 0xF8, 0xFA, ta), ZP + 0.2f);
        bb_font_draw(BB_FACE_BOLD, 12, tx + 6.0f, ty + 3.0f, rgba(0x10, 0x10, 0x14, ta), C2D_AlignLeft, (unlocked || cur->desc[0]) ? cur->name : "???", ZT + 0.25f);
        C2D_DrawRectSolid(tx + 6.0f, ty + 17.0f, ZP + 0.21f, tw - 12.0f, 1.0f, rgba(0x20, 0x20, 0x28, ta));
        snprintf(buf, sizeof(buf), "%s", cur->desc[0] ? cur->desc : "No way to earn this yet.");
        char lines[3][96];
        int n = wrap_px(BB_FACE_BODY, 10, buf, tw - 12.0f, lines, 2);
        for (int k = 0; k < n; k++) bb_font_draw(BB_FACE_BODY, 10, tx + 6.0f, ty + 20.0f + (float)k * 10.0f, rgba(0x30, 0x30, 0x38, ta), C2D_AlignLeft, lines[k], ZT + 0.25f);
    }
    back_button(px + 20.0f, 216.0f, pa);
    bb_ui_zone_add(BB_ZONE_BACK, px + 2.0f, 198.0f, 36.0f, 38.0f);
}

/* ---------------------------------------------------------------- CHARACTERS (lore.gd): the description alone on top, the photo on the bottom */

static const bb_lore_t* lore_ensure(bb_screens_t* sc) {
    const int li = sc->page % (sc->ex->lore_count ? sc->ex->lore_count : 1);
    const bb_lore_t* l = &sc->ex->lore[li];
    const int key = 1000 + li * 4 + g_outfit[li];
    if (sc->detail_key != key) {
        const bool same_char = sc->detail_key >= 1000 && sc->detail_key < 2000 && (sc->detail_key - 1000) / 4 == li;
        free_sheet(&sc->detail);
        char name[40];
        snprintf(name, sizeof(name), "%s", l->t3x);
        if (g_outfit[li] > 0) {   /* lore_<tag>_<k>.t3x */
            char* dot = strrchr(name, '.');
            if (dot) snprintf(dot, sizeof(name) - (size_t)(dot - name), "_%d.t3x", g_outfit[li]);
        }
        sc->detail = bb_extras_load_sheet(name);
        if (!sc->detail && g_outfit[li] > 0) sc->detail = bb_extras_load_sheet(l->t3x);
        sc->detail_key = key;
        if (!same_char) { bb_ui_enter(); g_char_t = bb_ui_time(); }
    }
    return l;
}

static void draw_characters(bb_screens_t* sc) {
    const bb_lore_t* l = lore_ensure(sc);
    char buf[96];
    bb_view_draw_bg();
    C2D_DrawRectSolid(0, 0, ZP - 0.02f, 400, 240, rgba(0, 0, 0, 0.6f));
    float a = bb_ui_appear(0.0f, 0.45f);
    u8 r, g, b;
    name_color(l->name, &r, &g, &b);
    char up[32];
    snprintf(up, sizeof(up), "%s", l->name);
    for (char* c = up; *c; c++) *c = (char)toupper((unsigned char)*c);
    const float tx = 16.0f + 24.0f * (1.0f - a), w = 368.0f;
    T(BB_FACE_HEAD, 40, tx, 0.0f, rgba(r, g, b, a), C2D_AlignLeft, up);
    snprintf(buf, sizeof(buf), "%s   %s", l->occupation, l->height);
    T(BB_FACE_BODY, 10, tx + 2.0f, 48.0f, gray(a), C2D_AlignLeft, buf);
    snprintf(buf, sizeof(buf), "%d / %d", (sc->page % (sc->ex->lore_count ? sc->ex->lore_count : 1)) + 1, sc->ex->lore_count);
    T(BB_FACE_BODY, 10, 388.0f, 8.0f, gray(1), C2D_AlignRight, buf);
    float y = 62.0f;
    TS(BB_FACE_BOLD, 14, tx, y, white(a), C2D_AlignLeft, "Origins");
    C2D_DrawRectSolid(tx, y + 17.0f, ZP, w, 1.0f, rgba(0x70, 0x70, 0x7C, a));
    y += 21.0f;
    y += (float)draw_wrapped(BB_FACE_BODY, 10, tx, y, 11.0f, w, rgba(0xC8, 0xC8, 0xD0, a), C2D_AlignLeft, l->lore, 6) * 11.0f + 4.0f;
    TS(BB_FACE_BOLD, 14, tx, y, white(a), C2D_AlignLeft, "Residence");
    C2D_DrawRectSolid(tx, y + 17.0f, ZP, w, 1.0f, rgba(0x70, 0x70, 0x7C, a));
    y += 21.0f;
    y += (float)draw_wrapped(BB_FACE_BODY, 10, tx, y, 11.0f, w, rgba(0xC8, 0xC8, 0xD0, a), C2D_AlignLeft, l->residence, 2) * 11.0f + 5.0f;
    TS(BB_FACE_BOLD, 14, tx, y, white(a), C2D_AlignLeft, "Hobbies");
    C2D_DrawRectSolid(tx, y + 17.0f, ZP, w, 1.0f, rgba(0x70, 0x70, 0x7C, a));
    y += 21.0f;
    char hb[200];
    snprintf(hb, sizeof(hb), "%s", l->hobbies);
    char* part = hb;
    for (int k = 0; k < 3 && part && y < 232.0f; k++) {
        char* sep = strstr(part, ";;");
        if (sep) *sep = 0;
        snprintf(buf, sizeof(buf), "- %.80s", part);
        T(BB_FACE_BODY, 10, tx + 2.0f, y, rgba(0xC8, 0xC8, 0xD0, a), C2D_AlignLeft, buf);
        y += 11.0f;
        part = sep ? sep + 2 : NULL;
    }
}

/* BOTTOM: the character on the checkerboard (lore.tscn CheckerBackground + Sawtooth), a soft glow in her colour, the original's wavy
 * caption ([wave amp=10 freq=4]), the outfit she is wearing, page dots and the arrows. Touching her (or A) changes the outfit. */
static void draw_characters_bottom(bb_screens_t* sc) {
    const bb_lore_t* l = lore_ensure(sc);
    const int n = sc->ex->lore_count > 0 ? sc->ex->lore_count : 1, li = sc->page % n;
    u8 r, g, b;
    name_color(l->name, &r, &g, &b);
    bb_view_draw_checker(0x60);
    const float a = bb_ui_appear(0.0f, 0.45f);
    /* the sawtooth band across the top, in her colour */
    C2D_DrawRectSolid(0, 0, ZP - 0.02f, 320, 20.0f, rgba(r, g, b, 0.85f));
    for (int k = 0; k < 20; k++) {
        const float x = (float)k * 16.0f;
        C2D_DrawTriangle(x, 20.0f, rgba(r, g, b, 0.85f), x + 16.0f, 20.0f, rgba(r, g, b, 0.85f), x + 8.0f, 27.0f, rgba(r, g, b, 0.85f), ZP - 0.02f);
    }
    for (int k = 0; k < 3; k++)   /* the Aesthetic colour bars */
        C2D_DrawRectSolid(268.0f + (float)k * 14.0f, 5.0f, ZP - 0.01f, 10.0f, 10.0f, rgba((u8)(r * (0.5f + 0.25f * k)), (u8)(g * (0.5f + 0.25f * k)), (u8)(b * (0.5f + 0.25f * k)), 1.0f));
    TS(BB_FACE_BOLD, 14, 10.0f, 1.0f, white(1), C2D_AlignLeft, "Characters");
    /* glow behind her */
    for (int k = 0; k < 4; k++) C2D_DrawCircleSolid(160.0f, 120.0f, ZP, 92.0f - 18.0f * (float)k, rgba(r, g, b, 0.10f * a));
    /* her, with the appear (rise + fade) and the outfit pulse */
    const float ap = bb_ui_ease_out_back(bb_ui_appear(0.0f, 0.4f));
    const float pe = (bb_ui_time() - g_outfit_t) / 0.2f;
    const float pulse = pe < 1.0f && pe >= 0.0f ? 1.1f - 0.1f * bb_ui_ease_out_back(pe) : 1.0f;
    const float bw = 236.0f * pulse, bh = 228.0f * pulse;   /* bigger, centred, a little lower (the band stays clear) */
    draw_fit(sc->detail, 160.0f - bw * 0.5f, 134.0f - bh * 0.5f + 22.0f * (1.0f - ap), bw, bh, 0.53f);   /* in front of the band, glow and caption */
    bb_ui_zone_add(BB_ZONE_LEVEL, 80.0f, 26.0f, 160.0f, 160.0f);   /* touch her: the outfit (cursor 0, so a tap presses at once) */
    /* the wavy caption, letter by letter */
    if (bb_font_ready() && l->caption[0]) {
        char cap[64];
        snprintf(cap, sizeof(cap), "\"%.58s\"", l->caption);
        int size = 14;
        while (size > 10 && bb_font_width(BB_FACE_BOLD, size, cap) > 296.0f) size--;
        float x = 160.0f - bb_font_width(BB_FACE_BOLD, size, cap) * 0.5f;
        C2D_DrawRectangle(0, 176.0f, ZP + 0.03f, 320, 30.0f, rgba(0, 0, 0, 0.0f), rgba(0, 0, 0, 0.0f), rgba(0, 0, 0, 0.6f), rgba(0, 0, 0, 0.6f));
        for (int i = 0; cap[i]; i++) {
            char ch[2] = { cap[i], 0 };
            const float dy = bb_ui_reduce_motion() ? 0.0f : sinf(bb_ui_time() * 4.0f * 3.14159f * 0.5f + (float)i * 0.55f) * 2.2f;
            bb_font_draw(BB_FACE_BOLD, size, x + 1.0f, 186.0f + dy + 1.0f, rgba(0, 0, 0, 0.8f * a), C2D_AlignLeft, ch, ZT - 0.01f);
            bb_font_draw(BB_FACE_BOLD, size, x, 186.0f + dy, rgba(0xFF, 0xFF, 0xFF, a), C2D_AlignLeft, ch, ZT);
            x += bb_font_width(BB_FACE_BOLD, size, ch);
        }
    }
    /* the outfit she is wearing */
    /* in front of everything: a dark pill with the three outfit dots and the current one's name */
    static const char* const k_outfit[3] = { "Clothed", "Lingerie", "Nude" };
    const float zf = 0.56f;
    rrect(204.0f, 206.0f, 98.0f, 30.0f, 8.0f, rgba(0, 0, 0, 0.78f * a), zf);
    for (int k = 0; k < 3; k++) {
        const bool on = k == g_outfit[li];
        const float ox = 225.0f + (float)k * 28.0f;
        C2D_DrawCircleSolid(ox, 214.0f, zf + 0.01f, on ? 4.0f : 2.5f, on ? rgba(r, g, b, 1.0f) : rgba(0x80, 0x80, 0x8A, 0.9f));
    }
    if (bb_font_ready()) bb_font_draw(BB_FACE_BOLD, 12, 253.0f, 219.0f, white(a), C2D_AlignCenter, k_outfit[g_outfit[li] % 3], zf + 0.02f);
    /* page dots, one per character (over the art too) */
    for (int k = 0; k < n; k++) C2D_DrawCircleSolid(160.0f - (float)(n - 1) * 5.0f + (float)k * 10.0f, 34.0f, zf, k == li ? 3.0f : 2.0f, k == li ? white(1) : rgba(0x90, 0x90, 0x9A, 0.8f));
    const float arrow = 3.0f * sinf(bb_ui_time() * 6.0f);
    T(BB_FACE_BOLD, 26, 18.0f - arrow, 96.0f, white(0.85f), C2D_AlignCenter, "<");
    T(BB_FACE_BOLD, 26, 302.0f + arrow, 96.0f, white(0.85f), C2D_AlignCenter, ">");
    bb_ui_zone_add(BB_ZONE_PREV, 0, 60.0f, 50.0f, 110.0f);
    bb_ui_zone_add(BB_ZONE_NEXT, 270.0f, 60.0f, 50.0f, 110.0f);
    back_button(22.0f, 216.0f, a);
    bb_ui_zone_add(BB_ZONE_BACK, 4.0f, 198.0f, 36.0f, 38.0f);
    (void)g_char_t;
}

/* PowerUpSelectionMenu: it opens over the difficulty menu (ColorRect dim). TOP = the SELECT DIFFICULTY title, dimmed. */
static void draw_powerup(bb_screens_t* sc) {
    (void)sc;
    bb_ui_difficulty_top(g_tb);
    C2D_DrawRectSolid(0, 0, 0.70f, 400, 240, rgba(0, 0, 0, 0.6f * bb_ui_appear(0.0f, 0.3f)));
}

/* Power-up descriptions as the original's BBCode colour runs (class/powerups.gd). */
typedef struct { const char* t; u32 c; } pu_run_t;
static int pu_runs(int item, pu_run_t* r) {
    const u32 w = 0xFFF8F4F4u, gold_c = 0xFF3CB7FFu, org = 0xFF00A5FFu, red = 0xFF0000FFu;   /* ABGR: white, ffb73c, orange, red */
    int n = 0;
#define RUN(txt, col) do { r[n].t = (txt); r[n].c = (col); n++; } while (0)
    switch (item) {
        case BB_ITEM_POWERUP_EXTRA_LIVES:  RUN("Get ", w); RUN("3", gold_c); RUN(" extra lives.", w); break;
        case BB_ITEM_POWERUP_NO_FAIL:      RUN("Misses don't do damage. ", w); RUN("0.5x", red); RUN(" score.", w); break;
        case BB_ITEM_POWERUP_DOUBLE_EDGED: RUN("Gives ", w); RUN("2x", org); RUN(" score. Take ", w); RUN("2x", red); RUN(" damage.", w); break;
        case BB_ITEM_POWERUP_BLINDFOLD:    RUN("3x", org); RUN(" Score. ", w); RUN("Notes disappear.", red); break;
        case BB_ITEM_POWERUP_HARDCORE:     RUN("4x", org); RUN(" score. Instant ", w); RUN("fail", red); RUN(" on any mistake.", w); break;
        default:                           RUN("Play the level with no power ups.", w); break;
    }
#undef RUN
    return n;
}

/* BOTTOM: the row of large power-up icons centred on the selection (others at 40 % alpha), each bobbing 10 px over 2 s with its own
 * phase (set_hover_offset 0.2 * (i + 1)); the header and caption fade in under the selected one (0.5 s expo). Touch zones per icon. */
void bb_screens_draw_powerup_bottom(bb_screens_t* sc, C2D_TextBuf tb) {
    g_tb = tb;
    bb_player_t* p = &sc->save->player;
    int rows[6], n = 0;
    rows[n++] = 0;
    for (int it = BB_ITEM_POWERUP_EXTRA_LIVES; it <= BB_ITEM_POWERUP_HARDCORE; it++) if (bb_player_has_item(p, it)) rows[n++] = it;
    const int cur = sc->cursor[SCN_POWERUP] < n ? sc->cursor[SCN_POWERUP] : 0;
    static float row_f = 0.0f, text_t = -10.0f;
    static int last = -1;
    if (bb_ui_enter_time() < 0.03f) { row_f = (float)cur; last = -1; }
    if (cur != last) { last = cur; text_t = bb_ui_time(); }
    bb_ui_smooth(&row_f, (float)cur, 14.0f);
    bb_view_draw_checker(0x90);
    C2D_DrawRectSolid(0, 0, ZP - 0.02f, 320, 240, rgba(0, 0, 0, 0.45f));
    const float a = bb_ui_appear(0.0f, 0.35f);
    for (int i = 0; i < n; i++) {
        const float d = (float)i - row_f;
        if (fabsf(d) > 2.4f) continue;
        const bool sel = i == cur;
        const float x = 160.0f + d * 112.0f;
        const float ph = bb_ui_reduce_motion() ? 0.0f : (bb_ui_time() + 0.2f * (float)(i + 1)) * 3.14159f;   /* 2 s period */
        const float y = 92.0f - 4.0f * (0.5f - 0.5f * cosf(ph));
        const float size = 96.0f - 18.0f * fminf(fabsf(d), 1.0f);
        const int art = rows[i] ? rows[i] - BB_ITEM_POWERUP_EXTRA_LIVES + 1 : 0;
        bb_art_powerup(art, x, y, size, C2D_Color32(255, 255, 255, (u8)(255.0f * a * (sel ? 1.0f : 0.4f))), ZI);
        bb_ui_zone_add(BB_ZONE_LEVEL + i, x - 44.0f, y - 48.0f, 88.0f, 96.0f);
    }
    /* header + caption (Encode Sans SC Condensed Bold 32 / Regular 24 in the original) */
    const float ta = (bb_ui_reduce_motion() ? 1.0f : 1.0f - powf(2.0f, -10.0f * (bb_ui_time() - text_t) / 0.5f)) * a;
    T(BB_FACE_BOLD, 21, 160.0f, 146.0f, white(ta), C2D_AlignCenter, bb_powerup_title(rows[cur]));
    pu_run_t r[6];
    const int nr = pu_runs(rows[cur], r);
    float w = 0.0f;
    for (int i = 0; i < nr; i++) w += bb_font_width(BB_FACE_BODY, 14, r[i].t);
    float x = 160.0f - w * 0.5f;
    for (int i = 0; i < nr; i++) {
        const u32 c = (r[i].c & 0x00FFFFFFu) | ((u32)(255.0f * ta) << 24);
        bb_font_draw(BB_FACE_BODY, 14, x, 174.0f, c, C2D_AlignLeft, r[i].t, ZT);
        x += bb_font_width(BB_FACE_BODY, 14, r[i].t);
    }
    if (cur > 0) T(BB_FACE_BOLD, 26, 16.0f, 78.0f, gray(a), C2D_AlignCenter, "<");
    if (cur + 1 < n) T(BB_FACE_BOLD, 26, 304.0f, 78.0f, gray(a), C2D_AlignCenter, ">");
    back_button(18.0f, 222.0f, bb_ui_appear(0.2f, 0.3f));
    bb_ui_zone_add(BB_ZONE_BACK, 0, 204.0f, 40.0f, 36.0f);
}

void bb_screens_draw(bb_screens_t* sc, C2D_TextBuf tb) {
    g_tb = tb;
    {
        static sc_node_t last_node = SCN_NONE;
        if (sc->node != last_node) { last_node = sc->node; bb_ui_enter(); }
    }
    switch (sc->node) {
        case SCN_SHOP: case SCN_SHOP_LIST: case SCN_SHOP_TALK: case SCN_SHOP_OUTFIT: draw_shop_top(sc); break;
        case SCN_EXTRAS: case SCN_ACH: bb_ui_logo_top_still(); break;   /* the main menu's top screen, held without a transition */
        case SCN_THEATER: draw_theater_top(sc); break;
        case SCN_CHARS: draw_characters(sc); break;
        case SCN_COLLECT: bb_coll_draw_top(); break;
        case SCN_MUSIC: bb_music_draw_top(); break;
        case SCN_SCENEVIEW: bb_sv_draw_top(); break;
        case SCN_GALLERY: case SCN_GALLERY_VIEW: bb_gal_draw_top(); break;
        case SCN_POWERUP: draw_powerup(sc); break;
        default: break;
    }
}

/* Bottom screen for everything but the shop and the power-up row: the interactive half of each screen, with its touch zones. */
static void draw_ui_bottom(bb_screens_t* sc) {
    switch (sc->node) {
        case SCN_EXTRAS: draw_extras(sc); break;
        case SCN_THEATER: draw_theater_bottom(sc); break;
        case SCN_CHARS: draw_characters_bottom(sc); break;
        case SCN_GALLERY: case SCN_GALLERY_VIEW: bb_gal_draw_bottom(); break;
        case SCN_ACH: draw_achievements(sc); break;
        case SCN_COLLECT: bb_coll_draw_bottom(); break;
        case SCN_MUSIC: bb_music_draw_bottom(); break;
        case SCN_SCENEVIEW: bb_sv_draw_bottom(); break;
        default: break;
    }
}
