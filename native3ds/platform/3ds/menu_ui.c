/**
 * @file menu_ui.c
 * @brief Original-style, animated top-screen menus (see menu_ui.h). Layouts follow the reference game's main menu,
 *        LevelSelect/volume map, DifficultyMenu/ModifiersMenu and Settings screens; type is the original Encode Sans SC
 *        Condensed / Anton (font.c); motion comes from ui_motion.c (staggered entrances, eased cursors, pulses).
 */
#include "menu_ui.h"
#include "game_view.h"
#include "ui_art.h"
#include "ui_motion.h"
#include "font.h"
#include "progress.h"
#include "theme.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define ZP 0.35f
#define ZI 0.45f
#define ZT 0.50f
#define ZD 0.60f   /* dialogs sit above the dimmed main-menu backdrop */

static u32 rgba(u8 r, u8 g, u8 b, float a) { return C2D_Color32(r, g, b, (u8)(a < 0.0f ? 0.0f : (a > 1.0f ? 255.0f : a * 255.0f))); }
static u32 white(float a)  { return rgba(0xF4, 0xF4, 0xF8, a); }
static u32 dim(float a)    { return rgba(0x86, 0x86, 0x92, a); }
static u32 orange(float a) { return rgba(0xFF, 0x8A, 0x1E, a); }
static u32 gold(float a)   { return rgba(0xFF, 0xC8, 0x3C, a); }

/* font-aware text: face/size explicit; falls back to the system font through bb_view_text when the atlas is missing */
static void tx(C2D_TextBuf tb, bb_face_t face, int size, float x, float y, u32 c, u32 align, const char* s) {
    if (bb_font_ready()) {
        bb_font_draw(face, size, x, y, c, align, s, ZT);
    } else if (tb) {
        bb_view_text(tb, x, y, (float)size / 27.0f, c, align, s, ZT);
    }
}

static void upper(char* dst, size_t n, const char* src) {
    size_t i = 0;
    for (; src[i] && i + 1 < n; i++) dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

static bool g_set_bottom;
static void footer(C2D_TextBuf tb, const char* s) {
    if (g_set_bottom) return;   /* the bottom-screen dialog fills the screen: no footer line */
    float a = bb_ui_appear(0.25f, 0.3f);
    tx(tb, BB_FACE_BODY, 12, 200.0f, 224.0f, rgba(0xC8, 0xC8, 0xD8, 0.8f * a), C2D_AlignCenter, s);
}

static void gear(float cx, float cy, float r, float angle, u32 col) {
    C2D_DrawCircleSolid(cx, cy, ZP, r, col);
    for (int k = 0; k < 8; k++) {
        float a = angle + (float)k * 0.785398f;
        float c = cosf(a), s = sinf(a);
        float px = -s, py = c;
        C2D_DrawTriangle(cx + c * (r + 4.0f) + px * 3.5f, cy + s * (r + 4.0f) + py * 3.5f, col,
                         cx + c * (r + 4.0f) - px * 3.5f, cy + s * (r + 4.0f) - py * 3.5f, col,
                         cx + c * (r - 2.0f), cy + s * (r - 2.0f), col, ZP);
    }
    C2D_DrawCircleSolid(cx, cy, ZP + 0.002f, r * 0.38f, rgba(0x10, 0x10, 0x14, 1.0f));
}

/* ------------------------------------------------------------------ shared colours */
void bb_ui_character_rgb(const char* character, u8* r, u8* g, u8* b) {
    u8 cr = 0x9A, cg = 0x92, cb = 0xA6;                    /* default: Cathy's grey-lavender */
    if (!character) character = "";
    if (strcmp(character, "Zoe") == 0) { cr = 0x8A; cg = 0x5A; cb = 0xE8; }
    else if (strcmp(character, "Gale") == 0) { cr = 0xD8; cg = 0x9A; cb = 0x82; }
    else if (strcmp(character, "Claire") == 0) { cr = 0xC8; cg = 0x6A; cb = 0x5A; }
    else if (strcmp(character, "Dawna") == 0) { cr = 0xEE; cg = 0xBC; cb = 0x94; }
    else if (strcmp(character, "Makayla") == 0) { cr = 0xFF; cg = 0xC8; cb = 0x9F; }
    *r = cr; *g = cg; *b = cb;
}

const char* bb_ui_grade_name(int g) {
    static const char* const gn[8] = { "", "F", "D", "C", "B", "A", "S", "SS" };
    return gn[g < 0 ? 0 : (g > 7 ? 7 : g)];
}

void bb_ui_grade_rgb(int g, u8 out[3]) {
    static const u8 gc[8][3] = { { 0x58, 0x58, 0x62 }, { 0xF0, 0x40, 0x4C }, { 0xFF, 0xA0, 0x50 }, { 0xE8, 0xE8, 0xF0 }, { 0x6C, 0xD8, 0xFF }, { 0x86, 0xF0, 0x96 }, { 0xFF, 0xDC, 0x50 }, { 0xFF, 0xDC, 0x50 } };
    g = g < 0 ? 0 : (g > 7 ? 7 : g);
    out[0] = gc[g][0]; out[1] = gc[g][1]; out[2] = gc[g][2];
}

/* ------------------------------------------------------------------ atmosphere */
/* ------------------------------------------------------------------ main menu (dual screen) */
/* TOP: the standard checker backdrop with the big Beat Banger logo breathing on the menu music's beat (128 BPM).
 * BOTTOM: the original's main-menu video (zoe_fix_main_menu.ogv, background_video.gd) full screen with the menu words over its dark
 * left side; every word is a touch target and so is the original's BoobClick area (5 taps -> the naked loop, stars at the touch). */
/* main_menu.tscn MenuButtons order: MENU_PLAY, MENU_OPTIONS, MENU_MODS, MENU_SHOP, MENU_EXTRAS, MENU_ACHIEVEMENTS, MENU_QUIT */
static const char* const k_main_items[BB_MAIN_ITEMS] = { "Play", "Options", "Mods", "Shop", "Extras", "Achievements", "Quit" };
static float g_item_off[BB_MAIN_ITEMS];
static float g_theme_flash = -10.0f;   /* the Change Theme button lights when pressed */

/* "3DS Edition" in the menu font (Encode Sans bold small caps), right under every Beat Banger logo */
static void edition_tag(float cx, float y, int size, float alpha, float z, float pulse) {
    if (!bb_font_ready()) return;
    /* `pulse` = the logo's beat scale: the tag grows with it, about its own centre (a view transform keeps it smooth) */
    const float my = y + (float)size * 0.6f;
    C2D_ViewReset();
    C2D_ViewTranslate(cx * (1.0f - pulse), my * (1.0f - pulse));
    C2D_ViewScale(pulse, pulse);
    bb_font_draw_smallcaps(BB_FACE_BOLD, size, cx + 1.5f, y + 1.5f, rgba(0, 0, 0, 0.7f * alpha), C2D_AlignCenter, "3DS Edition", z - 0.001f);
    bb_font_draw_smallcaps(BB_FACE_BOLD, size, cx, y, rgba(0xF4, 0xF4, 0xF8, alpha), C2D_AlignCenter, "3DS Edition", z);
    C2D_ViewReset();
}

static bool g_top_still = false;   /* bb_ui_logo_top_still: the logo is already in place (no entry animation) */
static void main_top(u8 dim_level) {
    bb_view_draw_checker(0x30);   /* the standard checker, close to its in-game brightness */
    const float v = (float)dim_level / 255.0f;
    const float la = g_top_still ? 1.0f : bb_ui_ease_out_back(bb_ui_appear(0.05f, 0.55f));
    const float bp = 1.0f + 0.035f * bb_ui_beat_pulse();
    const float lw = 300.0f * bp * (0.85f + 0.15f * la), lh = lw * 200.0f / 350.0f;
    const float cy = 112.0f - 18.0f * (1.0f - la);
    /* everything stays below z 0.35, so the achievements panel and the settings dialog draw over it */
    for (int k = 0; k < 4; k++) C2D_DrawCircleSolid(200.0f, cy, 0.305f, 150.0f - 24.0f * (float)k, rgba(0x30, 0x34, 0x60, 0.06f * v * la));
    bb_art_logo(200.0f - lw * 0.5f, cy - lh * 0.5f, lw, 0.31f);
    edition_tag(200.0f, cy + lh * 0.5f - 2.0f, 21, la, 0.312f, bp);   /* pulses with the logo */
    if (dim_level < 255) C2D_DrawRectSolid(0, 0, 0.32f, 400, 240, C2D_Color32(0, 0, 0, (u8)(255 - dim_level)));
}

static void main_backdrop(C2D_TextBuf tb, u8 dim_level, bool show_items, int cursor) {
    (void)tb; (void)show_items; (void)cursor;
    main_top(dim_level);
}

void bb_ui_main_menu(C2D_TextBuf tb, int cursor, int rank, int money, int exp) {
    (void)rank; (void)money; (void)exp; (void)cursor;   /* the original main menu shows no wallet/rank (footage 0:00-0:14) */
    main_top(255);
    (void)tb;
    /* the port credit, main menu top screen only (the old hint line's size and font) */
    if (bb_font_ready()) bb_font_draw(BB_FACE_BODY, 12, 8.0f, 224.0f, rgba(0xC8, 0xC8, 0xD8, 0.8f * bb_ui_appear(0.25f, 0.3f)), C2D_AlignLeft, "Port by CacheVex117", 0.33f);
}

void bb_ui_main_backdrop(C2D_TextBuf tb, u8 dim_level) { main_backdrop(tb, dim_level, false, -1); }

static float item_y(int i) { return 24.0f + (float)i * 28.0f; }   /* moved up for the seventh entry */
float bb_ui_main_item_y(int i) { return item_y(i); }
static int   item_size(bool sel) { return sel ? 21 : 17; }

/* Star burst (background_video.gd StarExplode, CPUParticles2D at 1080p mapped to the 240-px screen): 8 score-screen stars per tap,
 * 500 px/s in every direction, gravity 500 px/s^2, 1 s life, scale 0.52 -> 0.30 of the 100 px star, fading out, random spin. */
#define EGG_MAX_P 48
typedef struct { float x, y, vx, vy, ang, spin, t; bool live; } egg_particle_t;
static egg_particle_t g_egg_p[EGG_MAX_P];
static int g_egg_taps = 0;
static float g_egg_shutter_t = -10.0f;
static uint32_t g_egg_rng = 0x2545F491u;
static float egg_rand(void) { g_egg_rng = g_egg_rng * 1664525u + 1013904223u; return (float)(g_egg_rng >> 8) / 16777216.0f; }

static void egg_burst(float x, float y) {
    const float K = 240.0f / 1080.0f;
    int spawned = 0;
    for (int i = 0; i < EGG_MAX_P && spawned < 8; i++) {
        if (g_egg_p[i].live) continue;
        const float dir = egg_rand() * 6.2831853f;
        g_egg_p[i] = (egg_particle_t){ x, y, cosf(dir) * 500.0f * K, sinf(dir) * 500.0f * K, (-285.0f + 495.0f * egg_rand()) * 0.0174533f,
                                       143.5f * egg_rand() * 0.0174533f, 0.0f, true };
        spawned++;
    }
}

static void egg_draw(void) {
    const float K = 240.0f / 1080.0f, dt = bb_ui_dt();
    for (int i = 0; i < EGG_MAX_P; i++) {
        egg_particle_t* q = &g_egg_p[i];
        if (!q->live) continue;
        q->t += dt;
        if (q->t >= 1.0f) { q->live = false; continue; }
        q->vy += 500.0f * K * dt;
        q->x += q->vx * dt;
        q->y += q->vy * dt;
        q->ang += q->spin * dt;
        const float sc = 0.52f + (0.30f - 0.52f) * q->t;
        bb_art_star_rot(false, q->x, q->y, 100.0f * sc * K * 1.25f, q->ang, C2D_Color32(255, 255, 255, (u8)(255.0f * (1.0f - q->t))), 0.80f);
    }
    /* MainMenuShutters "shutters": two black halves meet in the middle and slide out over 0.3 s */
    const float st = bb_ui_time() - g_egg_shutter_t;
    if (st >= 0.0f && st < 0.3f) {
        const float off = 120.0f * (st / 0.3f);
        C2D_DrawRectSolid(0, -off, 0.85f, 320, 120, C2D_Color32(0, 0, 0, 0xFF));
        C2D_DrawRectSolid(0, 120.0f + off, 0.85f, 320, 120, C2D_Color32(0, 0, 0, 0xFF));
    }
}

void bb_ui_main_menu_bottom(int cursor, u8 dim_level, bool show_items) {
    const float ca = bb_ui_appear(0.0f, 0.5f);
    bb_art_mainmenu(bb_ui_time(), 0.0f, 0.0f, (u8)((float)dim_level * ca), 0.20f);
    /* the original's left gradient keeps the menu words readable over the stage */
    C2D_DrawRectangle(0, 0, 0.25f, 170.0f, 240.0f, C2D_Color32(0, 0, 0, 0xC8), C2D_Color32(0, 0, 0, 0), C2D_Color32(0, 0, 0, 0xC8), C2D_Color32(0, 0, 0, 0));
    for (int i = 0; i < BB_MAIN_ITEMS && show_items; i++) {
        const bool sel = i == cursor;
        const float a = bb_ui_appear(0.1f + 0.05f * (float)i, 0.35f);
        bb_ui_smooth(&g_item_off[i], sel ? 9.0f : 0.0f, 20.0f);
        const float x = 14.0f + g_item_off[i] - 26.0f * (1.0f - a);
        if (sel) C2D_DrawRectSolid(0, item_y(i) + 3.0f, ZT - 0.01f, 4.0f, 18.0f, orange(a));
        if (bb_font_ready()) bb_font_draw_smallcaps(BB_FACE_BOLD, item_size(sel), x, item_y(i) + (sel ? 0.0f : 2.0f), sel ? white(a) : rgba(0xB4, 0xB4, 0xBE, a), C2D_AlignLeft, k_main_items[i], ZT);
    }
    egg_draw();
    if (show_items) {   /* ChangeTheme (MainMenuButtonIcon, bottom right): the icon at DIM_GRAY, its label beside it; X or a touch */
        const float a = bb_ui_appear(0.4f, 0.35f);
        const bool lit = bb_ui_time() - g_theme_flash < 0.3f;
        /* white (not dimmed), no label; a short pop when pressed */
        bb_art_theme_icon(298.0f, 218.0f, lit ? 30.0f : 26.0f, C2D_Color32(255, 255, 255, (u8)(255.0f * a)), ZT);
        if (bb_font_ready()) bb_font_draw(BB_FACE_BODY, 10, 280.0f, 220.0f, rgba(0x80, 0x80, 0x88, 0.8f * a), C2D_AlignRight, "X", ZT);
        bb_ui_zone_add(BB_ZONE_THEME, 236.0f, 200.0f, 84.0f, 40.0f);
    }
    if (dim_level < 255) C2D_DrawRectSolid(0, 0, 0.90f, 320, 240, C2D_Color32(0, 0, 0, (u8)(255 - dim_level)));
}

void bb_ui_theme_pressed(void) { g_theme_flash = bb_ui_time(); }

int bb_ui_main_menu_hit(int px, int py, int cursor) {
    for (int i = 0; i < BB_MAIN_ITEMS; i++) {
        const float w = bb_font_ready() ? bb_font_width_smallcaps(BB_FACE_BOLD, item_size(i == cursor), k_main_items[i]) : 90.0f;
        const float y = item_y(i);
        if ((float)py >= y - 3.0f && (float)py < y + 25.0f && (float)px < 14.0f + g_item_off[i] + w + 10.0f) return i;
    }
    return -1;
}

int bb_ui_egg_tap(int px, int py) {
    if (bb_theme_halloween()) return 0;   /* background_video._on_boob_click_button_down: only in the default theme */
    /* BoobClick: offsets (984,372)-(1459,691) on the 1920x1080 video, mapped through the crop the loop was built with */
    float cx, cy, cw;
    bb_art_mainmenu_crop(&cx, &cy, &cw);
    const float k = 320.0f / cw;
    const float x0 = (984.0f - cx) * k, x1 = (1459.0f - cx) * k, y0 = (372.0f - cy) * k, y1 = (691.0f - cy) * k;
    if (g_egg_taps >= 5 || (float)px < x0 || (float)px >= x1 || (float)py < y0 || (float)py >= y1) return 0;   /* boob_click.disabled after 5 */
    g_egg_taps++;
    egg_burst((float)px, (float)py);   /* star_explode.global_position = the touch */
    if (g_egg_taps == 5) {             /* switch_to_naked_loop */
        g_egg_shutter_t = bb_ui_time();
        bb_art_mainmenu_variant(1);
    }
    return g_egg_taps;
}

void bb_ui_egg_reset(void) {
    g_egg_taps = 0;
    bb_art_mainmenu_variant(0);
}

/* Boot title screen: the stage loop, a big logo that drops in with a light sweep, and a pulsing PRESS A. */
/* ------------------------------------------------------------------ level select (bottom screen, touch) */
static void rank_block(C2D_TextBuf tb, int exp, float right_x) {
    char buf[128], up[128];
    float a = bb_ui_appear(0.1f, 0.4f);
    float x = right_x + 40.0f * (1.0f - a);
    int rank = bb_rank_from_exp(exp), next = bb_rank_next(exp);
    upper(up, sizeof(up), bb_rank_title(rank));
    snprintf(buf, sizeof(buf), "RANK : %.100s", up);
    tx(tb, BB_FACE_BOLD, 12, x, 5.0f, gold(a), C2D_AlignRight, buf);
    float frac = 1.0f;
    if (next != rank) {
        int lo = bb_rank_required_exp(rank), hi = bb_rank_required_exp(next);
        frac = hi > lo ? (float)(exp - lo) / (float)(hi - lo) : 1.0f;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
    }
    float fill = frac * bb_ui_appear(0.35f, 0.6f);   /* the bar fills in on entry */
    C2D_DrawRectSolid(x - 110.0f, 22.0f, ZP, 110.0f, 4.0f, rgba(0xFF, 0xFF, 0xFF, 0.22f * a));
    C2D_DrawRectSolid(x - 110.0f, 22.0f, ZP + 0.01f, 110.0f * fill, 4.0f, white(a));
    if (next != rank) {
        upper(up, sizeof(up), bb_rank_title(next));
        snprintf(buf, sizeof(buf), "NEXT RANK: %.100s", up);
        tx(tb, BB_FACE_BODY, 10, x, 28.0f, dim(a), C2D_AlignRight, buf);
    }
}

/* BeatSyncComponent on LevelNode: on every beat of the preview the node's Root scales +0.15 and its white outline +0.1 (of 0.625),
 * then both spring back over 0.4 s with TRANS_BACK / EASE_OUT (a small undershoot). Returns the extra scale for `amount`. */
static float beat_sync(float amount, float time_s) {
    const float t = bb_ui_beat_since(128.0f);
    if (t >= time_s) return 0.0f;
    return amount * (1.0f - bb_ui_ease_out_back(t / time_s));
}

static float g_lvl_drag = 0.0f;   /* touch drag offset in portrait steps (main.c sets it while the stylus scrolls) */
void bb_ui_level_drag(float steps) { g_lvl_drag = steps; }

void bb_ui_level_select(C2D_TextBuf tb, const bb_ui_level_t* lv, int n, int cursor, const char* act_name, int act_idx,
                        int act_count, int exp, const bool owned_powerups[5]) {
    static float lvl_f = 0.0f;
    static int last_cursor = -1, last_act = -1;
    static float change_t = -10.0f;
    char buf[64], up[64];
    const float W = 320.0f, CX = 160.0f, STEP = 112.0f;
    bb_view_draw_checker(0x90);
    if (bb_theme_halloween()) {
        /* level_select.tscn SeasonalOverlay/Halloween (1080p -> the 4:3 bottom at K = 240/1080, x 240..1680): SpookyTree2 (304, 712)
         * rot 15 deg, SpookyTree4 (1632, 728) rot -15 deg in the lower corners, behind the portraits; the faint webs SimpleWeb2
         * (alpha 0.173) top left and SimpleWeb1 (alpha 0.141) bottom right, pulled inside the cropped screen */
        const float k = 240.0f / 1080.0f;
        bb_art_hw_level_deco(0, 304.0f * k - 53.3f + 6.0f, 712.0f * k + 18.0f, 1.0f, 0.261799f, 0xFFFFFFFFu, 0.12f);
        bb_art_hw_level_deco(1, 1632.0f * k - 53.3f - 6.0f, 728.0f * k + 18.0f, 1.0f, -0.261799f, 0xFFFFFFFFu, 0.12f);
        bb_art_hw_level_deco(3, 30.0f, 33.0f, 1.0f, 0.0f, C2D_Color32(0xFF, 0xFF, 0xFF, (u8)(0.173f * 255.0f)), 0.12f);
        bb_art_hw_level_deco(2, 290.0f, 212.0f, 1.0f, 0.0f, C2D_Color32(0xFF, 0xFF, 0xFF, (u8)(0.141f * 255.0f)), 0.12f);
    }

    if (last_act != act_idx) { last_act = act_idx; lvl_f = (float)cursor; }
    if (cursor != last_cursor) { last_cursor = cursor; change_t = bb_ui_time(); }
    bb_ui_smooth(&lvl_f, (float)cursor, 14.0f);
    const float view_f = lvl_f - g_lvl_drag;

    float ta = bb_ui_appear(0.0f, 0.4f);
    snprintf(buf, sizeof(buf), "VOLUME %d", act_idx + 1);
    tx(tb, BB_FACE_HEAD, 28, 8.0f - 30.0f * (1.0f - ta), 0.0f, white(ta), C2D_AlignLeft, buf);
    upper(up, sizeof(up), act_name);
    tx(tb, BB_FACE_BOLD, 10, 9.0f - 30.0f * (1.0f - ta), 31.0f, orange(ta), C2D_AlignLeft, up);
    rank_block(tb, exp, 312.0f);
    if (owned_powerups) {   /* owned power-ups under the volume title */
        int shown = 0;
        float pa = bb_ui_appear(0.4f, 0.4f);
        for (int i = 0; i < 5; i++) {
            if (!owned_powerups[i]) continue;
            bb_art_shop_icon(BB_ITEM_POWERUP_EXTRA_LIVES + i, 0, 16.0f + (float)shown * 15.0f, 52.0f - 6.0f * (1.0f - pa), 14.0f, C2D_Color32(255, 255, 255, (u8)(255.0f * pa)), ZI);
            shown++;
        }
    }

    if (n > 0) {
        const float cy = 104.0f;
        const float grow = beat_sync(0.15f, 0.4f), ring_grow = beat_sync(0.16f, 0.4f);
        for (int pass = 0; pass < 2; pass++) {   /* neighbours first, the selected portrait on top */
            for (int i = 0; i < n; i++) {
                float d = (float)i - view_f;
                float ad = fabsf(d);
                if (ad > 2.2f) continue;
                const bool sel = i == cursor;
                if ((pass == 1) != sel) continue;
                float diam = ad < 1.0f ? 112.0f - 40.0f * ad : 72.0f - 14.0f * (ad - 1.0f);
                float cx = CX + d * STEP;
                float appear = bb_ui_appear(0.12f + 0.06f * fminf(ad, 3.0f), 0.4f);
                float yy = cy + 8.0f * (1.0f - appear);
                const float s = 1.0f + grow;                       /* every node pulses on the beat (LevelNode Root) */
                const float rr = (diam * 0.5f + 4.0f) * s * (1.0f + ring_grow);
                /* highlight(): modulate 1 for the selected level, 0.2 for the others */
                const float bright = sel ? 1.0f : 0.3f;
                u8 v = (u8)(255.0f * bright);
                C2D_DrawCircleSolid(cx, yy + 4.0f, ZP - 0.01f, rr, rgba(0, 0, 0, 0.45f * appear));                     /* Shadow */
                C2D_DrawCircleSolid(cx, yy, ZP, rr, sel ? (bb_theme_halloween() ? rgba(0xFF, 0x6C, 0x00, appear) : white(appear))   /* WhiteOutline */
                                                         : rgba(0x58, 0x58, 0x62, appear));                                 /* (LevelNode: #ff6c00 in Halloween) */
                C2D_DrawCircleSolid(cx, yy, ZP + 0.01f, (diam * 0.5f + 1.0f) * s, rgba(0x08, 0x08, 0x0C, appear));     /* Backing */
                u32 tint = lv[i].open ? C2D_Color32(v, v, v, (u8)(255.0f * appear)) : C2D_Color32(0x30, 0x30, 0x38, (u8)(255.0f * appear));
                bb_art_thumb(lv[i].id, cx, yy, (diam - 2.0f) * s, tint, ZI);
                if (!lv[i].open) tx(tb, BB_FACE_HEAD, 20, cx, yy - 13.0f, rgba(0x90, 0x90, 0x9A, appear), C2D_AlignCenter, "?");
                bb_ui_zone_add(BB_ZONE_LEVEL + i, cx - diam * 0.5f, yy - diam * 0.5f, diam, diam);
            }
        }
        const bb_ui_level_t* s = &lv[cursor];
        float since = bb_ui_time() - change_t;
        float na = bb_ui_ease_out(since / 0.28f) * bb_ui_appear(0.2f, 0.3f);
        for (int k = 0; k < 3; k++) {
            float pop = bb_ui_ease_out_back((since - 0.06f * (float)k) / 0.3f);
            bb_art_star(k >= s->diffs_done, CX + (float)(k - 1) * 17.0f, 172.0f, 15.0f * (0.4f + 0.6f * pop), 0xFFFFFFFFu, ZI);
        }
        upper(up, sizeof(up), s->character);
        {
            u8 cr, cg, cb;
            bb_ui_character_rgb(s->character, &cr, &cg, &cb);
            tx(tb, BB_FACE_HEAD, 28, CX, 180.0f + 8.0f * (1.0f - na), rgba(cr, cg, cb, na), C2D_AlignCenter, up);
        }
        upper(up, sizeof(up), s->open ? s->title : "LOCKED");
        tx(tb, BB_FACE_BOLD, 14, CX, 213.0f + 6.0f * (1.0f - na), white(na), C2D_AlignCenter, up);
        float arrow = 3.0f * sinf(bb_ui_time() * 6.0f);
        if (cursor > 0) { tx(tb, BB_FACE_BOLD, 26, 18.0f - arrow, 86.0f, dim(1), C2D_AlignCenter, "<"); bb_ui_zone_add(BB_ZONE_PREV, 0, 70.0f, 36.0f, 70.0f); }
        if (cursor + 1 < n) { tx(tb, BB_FACE_BOLD, 26, W - 18.0f + arrow, 86.0f, dim(1), C2D_AlignCenter, ">"); bb_ui_zone_add(BB_ZONE_NEXT, W - 36.0f, 70.0f, 36.0f, 70.0f); }
    }
    /* round back button (touch) */
    bb_art_icon(7, 18.0f, 222.0f, 24.0f, C2D_Color32(255, 255, 255, (u8)(255.0f * bb_ui_appear(0.3f, 0.3f))), ZI + 0.02f);
    bb_ui_zone_add(BB_ZONE_BACK, 0, 204.0f, 40.0f, 36.0f);
}

/* The framed shot of a level's character from its pre-level cutscene (white border, small drop shadow): the level-select top screen and
 * the loading screen's bottom screen. One 512x256 ETC1 panel (~64 KiB), loaded when the stage / image changes. */
static C2D_SpriteSheet g_card;
static char g_card_key[96];
static float g_card_t = -10.0f;

void bb_ui_level_card_free(void) {
    if (g_card) { C2D_SpriteSheetFree(g_card); g_card = NULL; }
    g_card_key[0] = 0;
}

static void framed_shot(const char* stage_id, int image, bool open, float cx, float cy, float w0, float z) {
    char key[96];
    snprintf(key, sizeof(key), "romfs:/stages/%s/cutscene/pre_%d.t3x", stage_id ? stage_id : "", image < 0 ? 0 : image);
    if (strcmp(key, g_card_key) != 0) {   /* drawn after C3D_FrameBegin (SYNCDRAW): the GPU is done with the previous shot */
        if (g_card) { C2D_SpriteSheetFree(g_card); g_card = NULL; }
        snprintf(g_card_key, sizeof(g_card_key), "%s", key);
        g_card = stage_id ? C2D_SpriteSheetLoad(key) : NULL;
        g_card_t = bb_ui_time();
    }
    if (!g_card) return;
    const float e = bb_ui_reduce_motion() ? 1.0f : bb_ui_ease_out((bb_ui_time() - g_card_t) / 0.3f);
    const float s = 0.94f + 0.06f * e;
    const float w = w0 * s, h = w * 225.0f / 400.0f, x = cx - w * 0.5f, y = cy - h * 0.5f, b = 3.0f;
    C2D_DrawRectSolid(x - b + 5.0f, y - b + 6.0f, z, w + 2.0f * b, h + 2.0f * b, rgba(0, 0, 0, 0.45f * e));   /* shadow */
    C2D_DrawRectSolid(x - b, y - b, z + 0.01f, w + 2.0f * b, h + 2.0f * b, white(e));                         /* white border */
    C2D_Image img = C2D_SpriteSheetGetImage(g_card, 0);
    Tex3DS_SubTexture st;
    const float tw = (float)img.tex->width, th = (float)img.tex->height;
    st.width = 400; st.height = 225;
    st.left = 0.0f; st.right = 400.0f / tw; st.top = 1.0f; st.bottom = 1.0f - 225.0f / th;
    img.subtex = &st;
    C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
    C2D_DrawParams dp;
    dp.pos.x = x; dp.pos.y = y; dp.pos.w = w; dp.pos.h = h;
    dp.center.x = 0.0f; dp.center.y = 0.0f; dp.depth = z + 0.02f; dp.angle = 0.0f;
    const u8 v = open ? 255 : 0x38;
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, C2D_Color32(v, v, v, (u8)(255.0f * e)), 1.0f);
    C2D_SetTintMode(C2D_TintMult);
    C2D_DrawImage(img, &dp, &t);
}

/* Top screen while choosing a level: the checker, the framed shot, and the volume markers (diamonds of the original volume map,
 * L / R switch volumes) under it. */
void bb_ui_level_card(const char* stage_id, int image, bool open, int act_idx, int act_count) {
    char buf[16];
    bb_view_draw_checker(0x40);
    framed_shot(stage_id, image, open, 200.0f, 104.0f, 300.0f, 0.40f);
    const float ta = bb_ui_appear(0.0f, 0.4f);
    const float ny = 208.0f, nsp = 44.0f;
    const float x0 = 200.0f - nsp * (float)(act_count - 1) * 0.5f, x1 = 200.0f + nsp * (float)(act_count - 1) * 0.5f;
    if (act_count > 1) C2D_DrawRectSolid(x0, ny - 1.0f, ZP, (x1 - x0) * ta, 2.0f, rgba(0xFF, 0xFF, 0xFF, 0.4f));
    for (int i = 0; i < act_count; i++) {
        const float x = act_count > 1 ? x0 + (x1 - x0) * (float)i / (float)(act_count - 1) : 200.0f;
        const bool on = i == act_idx;
        const float r = (on ? 7.0f + 0.8f * sinf(bb_ui_time() * 4.0f) : 4.5f) * ta;
        const u32 nc = on ? white(1) : rgba(0x70, 0x70, 0x80, 1);
        C2D_DrawTriangle(x, ny - r, nc, x - r, ny, nc, x + r, ny, nc, ZP + 0.01f);
        C2D_DrawTriangle(x, ny + r, nc, x - r, ny, nc, x + r, ny, nc, ZP + 0.01f);
        snprintf(buf, sizeof(buf), "VOL.%d", i + 1);
        tx(NULL, BB_FACE_BOLD, 12, x, ny + 10.0f, on ? white(ta) : dim(ta * 0.7f), C2D_AlignCenter, buf);
    }
    /* the shoulder buttons that switch volumes (the original shows its bumper glyphs at the ends of the volume map) */
    for (int k = 0; k < 2; k++) {
        const float bx = k ? 370.0f : 30.0f, by = ny + 2.0f, bw = 34.0f, bh = 17.0f;
        const u32 body = rgba(0xE6, 0xE6, 0xEE, ta), ink = rgba(0x10, 0x10, 0x16, ta);
        C2D_DrawRectSolid(bx - bw * 0.5f + 5.0f, by - bh * 0.5f, ZP + 0.02f, bw - 10.0f, bh, body);
        C2D_DrawCircleSolid(k ? bx + bw * 0.5f - 5.0f : bx - bw * 0.5f + 5.0f, by - bh * 0.5f + 5.0f, ZP + 0.02f, 5.0f, body);   /* rounded outer top corner */
        C2D_DrawRectSolid(k ? bx + bw * 0.5f - 10.0f : bx - bw * 0.5f, by - bh * 0.5f + 5.0f, ZP + 0.02f, 10.0f, bh - 5.0f, body);
        C2D_DrawRectSolid(k ? bx - bw * 0.5f : bx + bw * 0.5f - 5.0f, by - bh * 0.5f, ZP + 0.02f, 5.0f, bh, body);
        tx(NULL, BB_FACE_BOLD, 14, bx, by - 9.0f, ink, C2D_AlignCenter, k ? "R" : "L");
    }
}

/* Loading screen, bottom: the level's framed shot alone, with the spinner and LOADING of the original loader. `frozen` = the
 * single frame left on screen while the stage loads (the spinner cannot turn then). */
void bb_ui_loading_bottom(const char* stage_id, int image) {
    bb_view_draw_checker(0x40);
    framed_shot(stage_id, image, true, 160.0f, 108.0f, 280.0f, 0.40f);
    const float cx = 286.0f, cy = 222.0f;
    const float phase = bb_ui_time() / 0.9f - floorf(bb_ui_time() / 0.9f);
    for (int k = 0; k < 8; k++) {
        const float ang = ((float)k / 8.0f - phase) * 6.2831853f, f = 1.0f - (float)k / 8.0f;
        C2D_DrawCircleSolid(cx + cosf(ang) * 7.0f, cy + sinf(ang) * 7.0f, 0.6f, 1.1f + 1.4f * f, rgba(0xE8, 0xE8, 0xF0, (60.0f + 195.0f * f) / 255.0f));
    }
    tx(NULL, BB_FACE_BOLD, 12, 274.0f, 215.0f, rgba(0xA0, 0xA0, 0xB0, 1.0f), C2D_AlignRight, "LOADING");
}

/* Cutscene, bottom: the Beat Banger logo dimmed on the checker, and a quiet control strip: Skip / Autoplay / Next as text buttons with
 * their button glyphs (B, Y, A), hairline dividers, a soft flash where tapped, Autoplay lit (with ON) while it runs. Touch zones
 * BB_ZONE_LEVEL + 0..2. */
static float g_cs_flash[3] = { -10.0f, -10.0f, -10.0f };
void bb_ui_cutscene_tapped(int i) { if (i >= 0 && i < 3) g_cs_flash[i] = bb_ui_time(); }

void bb_ui_cutscene_bottom(bool autoplay) {
    bb_view_draw_checker(0x60);
    const float a = bb_ui_appear(0.0f, 0.4f);
    const float lw = 230.0f, lh = lw * 200.0f / 350.0f;
    bb_art_logo(160.0f - lw * 0.5f, 86.0f - lh * 0.5f, lw, 0.31f);
    edition_tag(160.0f, 86.0f + lh * 0.5f - 2.0f, 17, 1.0f, 0.312f, 1.0f);   /* this logo does not pulse */
    C2D_DrawRectSolid(0, 0, 0.32f, 320, 240, rgba(0, 0, 0, 0.62f));   /* the logo, dimmed */
    /* the strip */
    const float sy = 190.0f;
    C2D_DrawRectangle(0, sy - 14.0f, ZP, 320, 14.0f, rgba(0, 0, 0, 0.0f), rgba(0, 0, 0, 0.0f), rgba(0, 0, 0, 0.6f * a), rgba(0, 0, 0, 0.6f * a));
    C2D_DrawRectSolid(0, sy, ZP, 320, 240.0f - sy, rgba(0, 0, 0, 0.6f * a));
    C2D_DrawRectangle(0, sy, ZP + 0.01f, 160, 1.0f, rgba(0xFF, 0xFF, 0xFF, 0.0f), rgba(0xFF, 0xFF, 0xFF, 0.22f * a), rgba(0xFF, 0xFF, 0xFF, 0.0f), rgba(0xFF, 0xFF, 0xFF, 0.22f * a));
    C2D_DrawRectangle(160, sy, ZP + 0.01f, 160, 1.0f, rgba(0xFF, 0xFF, 0xFF, 0.22f * a), rgba(0xFF, 0xFF, 0xFF, 0.0f), rgba(0xFF, 0xFF, 0xFF, 0.22f * a), rgba(0xFF, 0xFF, 0xFF, 0.0f));
    for (int k = 1; k < 3; k++) C2D_DrawRectSolid(320.0f * (float)k / 3.0f, sy + 12.0f, ZP + 0.01f, 1.0f, 26.0f, rgba(0xFF, 0xFF, 0xFF, 0.12f * a));
    static const char* const lab[3] = { "Skip", "Autoplay", "Next" };
    static const char* const key[3] = { "B", "Y", "A" };
    for (int i = 0; i < 3; i++) {
        const float cx = 320.0f * ((float)i + 0.5f) / 3.0f, cy = sy + 25.0f;
        const bool lit = i == 1 && autoplay;
        /* tap flash: a soft light rising behind the label */
        const float fe = bb_ui_time() - g_cs_flash[i];
        if (fe >= 0.0f && fe < 0.35f) {
            const float f = 1.0f - fe / 0.35f;
            C2D_DrawRectangle(cx - 52.0f, sy + 1.0f, ZP + 0.015f, 104.0f, 49.0f, rgba(0xFF, 0xFF, 0xFF, 0.0f), rgba(0xFF, 0xFF, 0xFF, 0.0f), rgba(0xFF, 0xFF, 0xFF, 0.16f * f), rgba(0xFF, 0xFF, 0xFF, 0.16f * f));
        }
        const u32 ink = lit ? orange(a) : rgba(0xEC, 0xEC, 0xF2, 0.92f * a);
        const float tw = bb_font_ready() ? bb_font_width_smallcaps(BB_FACE_BOLD, 17, lab[i]) : 50.0f;
        const float gx = cx - (tw + 20.0f) * 0.5f;   /* glyph + gap + label, centred together */
        C2D_DrawCircleSolid(gx + 7.0f, cy, ZP + 0.02f, 7.0f, lit ? orange(a) : rgba(0xE6, 0xE6, 0xEE, 0.85f * a));
        if (bb_font_ready()) {
            bb_font_draw(BB_FACE_BOLD, 10, gx + 7.0f, cy - 7.0f, rgba(0x10, 0x10, 0x16, a), C2D_AlignCenter, key[i], ZT);
            bb_font_draw_smallcaps(BB_FACE_BOLD, 17, gx + 20.0f, cy - 11.0f, ink, C2D_AlignLeft, lab[i], ZT);
        }
        if (i == 1) {   /* the state under Autoplay, with a breathing dot while it runs */
            const float pulse = 0.5f + 0.5f * sinf(bb_ui_time() * 4.0f);
            if (lit) C2D_DrawCircleSolid(cx - 10.0f, sy + 42.0f, ZP + 0.02f, 2.0f, orange(a * (0.5f + 0.5f * pulse)));
            if (bb_font_ready()) bb_font_draw(BB_FACE_BOLD, 10, cx - 5.0f, sy + 36.0f, lit ? orange(a) : rgba(0x80, 0x80, 0x8A, a), C2D_AlignLeft, lit ? "ON" : "OFF", ZT);
        }
        bb_ui_zone_add(BB_ZONE_LEVEL + i, 320.0f * (float)i / 3.0f, sy - 6.0f, 320.0f / 3.0f, 240.0f - sy + 6.0f);
    }
}

/* Loading screen, top: the main-menu logo pulsing on the checker. */
void bb_ui_logo_top(void) { main_top(255); }
void bb_ui_logo_top_still(void) { g_top_still = true; main_top(255); g_top_still = false; }

/* ------------------------------------------------------------------ difficulty select */
/* TOP: the checker and the SELECT DIFFICULTY title only. */
void bb_ui_difficulty_top(C2D_TextBuf tb) {
    bb_view_draw_checker(0x60);
    const float ha = bb_ui_appear(0.0f, 0.4f);
    tx(tb, BB_FACE_HEAD, 40, 200.0f, 92.0f - 14.0f * (1.0f - ha), white(ha), C2D_AlignCenter, "SELECT DIFFICULTY");
    C2D_DrawRectSolid(200.0f - 120.0f * ha, 146.0f, ZP, 240.0f * ha, 1.0f, rgba(0xFF, 0xFF, 0xFF, 0.6f));
}

/* BOTTOM: the original DifficultyMenu as a carousel (DifficultyNode.tscn, checked against the running original): the highlighted
 * card centred with its translucent white outline, the others smaller and at half brightness beside it. A card: dark panel, the
 * difficulty name, five grade stars, the A / S / SS ribbons hanging from its top edge, a black diagonal slab carrying the character's
 * difficulty icon (pulsing +30 % on the preview's beat, BeatSyncComponent 0.3 / 0.5 s), and the HIGH SCORE / MAX COMBO bars under it.
 * PLAY HANDS FREE (or the autoplay requirement) floats above the highlighted card; MODIFIERS > and the back button sit in the corners. */
static void rounded(float x, float y, float w, float h, float r, u32 c, float z) {
    if (r * 2.0f > h) r = h * 0.5f;
    C2D_DrawRectSolid(x + r, y, z, w - 2.0f * r, h, c);
    C2D_DrawRectSolid(x, y + r, z, r, h - 2.0f * r, c);
    C2D_DrawRectSolid(x + w - r, y + r, z, r, h - 2.0f * r, c);
    C2D_DrawCircleSolid(x + r, y + r, z, r, c);
    C2D_DrawCircleSolid(x + w - r, y + r, z, r, c);
    C2D_DrawCircleSolid(x + r, y + h - r, z, r, c);
    C2D_DrawCircleSolid(x + w - r, y + h - r, z, r, c);
}

/* difficulty_ribbon: a 15x36 banner with a V notch (scale 1.27 x 1.22); filled = solid colour, empty = 50 % outline */
static void ribbon(float x, float y, float k, u32 col, bool filled, float z) {
    const float w = 15.0f * 1.27f * k, h = 36.0f * 1.22f * k, notch = 5.0f * 1.22f * k;
    if (filled) {
        C2D_DrawRectSolid(x, y, z, w, h - notch, col);
        C2D_DrawTriangle(x, y + h - notch, col, x + w * 0.5f, y + h - notch, col, x, y + h, col, z);
        C2D_DrawTriangle(x + w, y + h - notch, col, x + w * 0.5f, y + h - notch, col, x + w, y + h, col, z);
    } else {
        const u32 c = (col & 0x00FFFFFFu) | (0x80u << 24);
        C2D_DrawRectSolid(x, y, z, w, 1.0f, c);
        C2D_DrawRectSolid(x, y, z, 1.0f, h, c);
        C2D_DrawRectSolid(x + w - 1.0f, y, z, 1.0f, h, c);
    }
}

static void diff_card(const char* stage_id, int d, const bb_ui_diff_stat_t* st, float cx, float cy, float k, bool sel, float a, float icon_pulse) {
    static const char* const names[3] = { "Virgin", "Normal", "Expert" };
    char buf[24];
    const float s = 1.05f * k;                     /* DifficultyNode scale 1.05 */
    const float dimv = sel ? 1.0f : 0.5f;          /* highlight(): modulate 1 / 0.5 */
#define LX(v) (cx + (v) * s)
#define LY(v) (cy + (v) * s)
    if (sel) rounded(LX(-195), LY(-75), 390.0f * s, 166.0f * s, 5.0f * s, rgba(0xFF, 0xFF, 0xFF, 0.475f * a), ZP);    /* Outline */
    rounded(LX(-192), LY(-64), 384.0f * s, 152.0f * s, 5.0f * s, rgba(0x08, 0x08, 0x08, a), ZP + 0.01f);             /* Background */
    rounded(LX(-192), LY(-72), 384.0f * s, 144.0f * s, 5.0f * s, rgba((u8)(0x18 * dimv), (u8)(0x18 * dimv), (u8)(0x18 * dimv), a), ZP + 0.02f);   /* Foreground */
    {   /* the black slab: (64,-72) (192,-72) (192,72) (96,72) */
        const u32 k0 = rgba(0, 0, 0, a);
        C2D_DrawTriangle(LX(64), LY(-72), k0, LX(192), LY(-72), k0, LX(192), LY(72), k0, ZP + 0.03f);
        C2D_DrawTriangle(LX(64), LY(-72), k0, LX(192), LY(72), k0, LX(96), LY(72), k0, ZP + 0.03f);
    }
    const u8 iv = (u8)(255.0f * dimv);
    const float isz = 72.0f * s * (1.0f + icon_pulse);
    bb_art_diff_icon(stage_id, d, LX(136 + 3), LY(0), isz, C2D_Color32(iv, iv, iv, (u8)(255.0f * a)), ZI);
    const int fs = sel ? 17 : 14;
    tx(NULL, BB_FACE_BOLD, fs, LX(-160), LY(-23) - (float)fs * 0.62f, rgba((u8)(0xF4 * dimv), (u8)(0xF4 * dimv), (u8)(0xF8 * dimv), a), C2D_AlignLeft, names[d]);
    for (int i = 0; i < 5; i++) {   /* Stars at (-56, 28), 128 apart, scale 0.33 */
        const float pop = bb_ui_ease_out_back((bb_ui_enter_time() - 0.25f - 0.05f * (float)i) / 0.3f);
        const u8 v = (u8)(255.0f * dimv);
        bb_art_star(i >= st->grade, LX(-56 + (i - 2) * 128 * 0.33f), LY(28), 100.0f * 0.33f * s * (0.3f + 0.7f * pop), C2D_Color32(v, v, v, (u8)(255.0f * a)), ZI);
    }
    static const u32 rc[3] = { 0xFF00C3FEu, 0xFFFF9B30u, 0xFF3645EBu };   /* gold, blue, red (ABGR) */
    for (int r = 0; r < 3; r++) ribbon(LX(32 + 25 * r), LY(-80), s, (rc[r] & 0x00FFFFFFu) | ((u32)(255.0f * a * dimv) << 24), st->grade >= 5 + r, ZI + 0.01f);
    /* stats: HIGH SCORE bar (y 101..155) and MAX COMBO (+61), value panel 61..190 */
    for (int b = 0; b < 2; b++) {
        const float oy = b ? 61.0f : 0.0f;
        const u32 c1 = b ? rgba(0x95, 0x6F, 0x3F, a * dimv + (1.0f - dimv) * 0.5f * a) : rgba(0xAF, 0x72, 0x24, a * dimv + (1.0f - dimv) * 0.5f * a);
        const u32 c2 = b ? rgba(0x59, 0x3D, 0x1B, a) : rgba(0x69, 0x42, 0x11, a);
        rounded(LX(-190), LY(101 + oy), 380.0f * s, 54.0f * s, 5.0f * s, c1, ZP + 0.02f);
        rounded(LX(61), LY(101 + oy), 129.0f * s, 54.0f * s, 5.0f * s, c2, ZP + 0.03f);
        const int fz = sel ? 12 : 10;
        tx(NULL, BB_FACE_BOLD, fz, LX(-175), LY(128 + oy) - (float)fz * 0.66f, rgba(0xFF, 0xFF, 0xFF, a * (0.55f + 0.45f * dimv)), C2D_AlignLeft, b ? "MAX COMBO" : "HIGH SCORE");
        if (b) snprintf(buf, sizeof(buf), "x%d", st->combo);
        else snprintf(buf, sizeof(buf), "%06ld", (long)st->score);
        tx(NULL, BB_FACE_BOLD, fz, LX(126), LY(128 + oy) - (float)fz * 0.66f, rgba(0xFF, 0xFF, 0xFF, a * (0.55f + 0.45f * dimv)), C2D_AlignCenter, buf);
    }
#undef LX
#undef LY
}

void bb_ui_difficulty(C2D_TextBuf tb, const char* stage_id, const char* character, const char* title, const bb_ui_diff_stat_t st[3],
                      int cursor, float speed) {
    static float car = 0.0f;
    char buf[96], up[96];
    (void)character; (void)title;
    bb_view_draw_checker(0x90);
    if (bb_ui_enter_time() < 0.03f) car = (float)cursor;
    bb_ui_smooth(&car, (float)cursor, 14.0f);
    const float icon_pulse = (1.0f - bb_ui_ease_out_back(bb_ui_beat_since(128.0f) / 0.5f)) * 0.3f * (bb_ui_beat_since(128.0f) < 0.5f ? 1.0f : 0.0f);
    /* neighbours first, the highlighted card on top */
    for (int pass = 0; pass < 2; pass++) {
        for (int d = 0; d < 3; d++) {
            const bool sel = d == cursor;
            if ((pass == 1) != sel) continue;
            const float off = (float)d - car, ad = fabsf(off);
            if (ad > 1.6f) continue;
            const float k = 0.46f - 0.10f * fminf(ad, 1.0f);
            const float a = bb_ui_appear(0.06f + 0.06f * (float)d, 0.35f);
            const float cx = 160.0f + off * 178.0f + 24.0f * (1.0f - a), cy = 98.0f;
            diff_card(stage_id, d, &st[d], cx, cy, k, sel, a, sel ? icon_pulse : 0.0f);
            const float s = 1.05f * k;
            bb_ui_zone_add(BB_ZONE_LEVEL + d, cx - 195.0f * s, cy - 80.0f * s, 390.0f * s, 300.0f * s);
        }
    }
    /* HandsFree (above the card: panel -152..152 x -144..-88, black, white border) or the requirement text */
    const float ha2 = bb_ui_appear(0.3f, 0.35f);
    const float hover = 1.5f * sinf(bb_ui_time() * 3.0f);
    if (st[cursor].grade >= 4) {
        const float s = 1.05f * 0.46f, w = 304.0f * s, h = 56.0f * s, y = 98.0f - 144.0f * s + hover;
        rounded(160.0f - w * 0.5f - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, 4.0f, rgba(0xFF, 0xFF, 0xFF, 0.8f * ha2), ZP);
        rounded(160.0f - w * 0.5f, y, w, h, 4.0f, rgba(0, 0, 0, ha2), ZP + 0.01f);
        tx(tb, BB_FACE_BOLD, 12, 160.0f, y + h * 0.5f - 8.0f, white(ha2), C2D_AlignCenter, "PLAY HANDS FREE");
        bb_ui_zone_add(BB_ZONE_HANDSFREE, 160.0f - w * 0.5f, y - 4.0f, w, h + 8.0f);
    } else {
        tx(tb, BB_FACE_BOLD, 10, 160.0f, 22.0f + hover, rgba(0x7B, 0x7B, 0x7B, 0.76f * ha2), C2D_AlignCenter, "BEAT THE LEVEL WITH AT LEAST 4 STARS");
        tx(tb, BB_FACE_BOLD, 10, 160.0f, 33.0f + hover, rgba(0x7B, 0x7B, 0x7B, 0.76f * ha2), C2D_AlignCenter, "TO UNLOCK AUTOPLAY");
    }
    if (speed != 1.0f) {   /* SpeedModLabel */
        snprintf(buf, sizeof(buf), "SPEED = %.1f", (double)speed);
        upper(up, sizeof(up), buf);
        tx(tb, BB_FACE_BOLD, 14, 46.0f, 214.0f, orange(ha2), C2D_AlignLeft, up);
    }
    const float arrow = 2.0f * sinf(bb_ui_time() * 6.0f);
    tx(tb, BB_FACE_HEAD, 20, 294.0f, 207.0f, dim(ha2), C2D_AlignRight, "MODIFIERS");
    tx(tb, BB_FACE_BOLD, 21, 306.0f + arrow, 205.0f, white(ha2), C2D_AlignCenter, ">");
    bb_ui_zone_add(BB_ZONE_MODS, 190.0f, 200.0f, 130.0f, 40.0f);
    bb_art_icon(7, 18.0f, 222.0f, 24.0f, C2D_Color32(255, 255, 255, (u8)(255.0f * ha2)), ZI + 0.02f);
    bb_ui_zone_add(BB_ZONE_BACK, 0, 204.0f, 40.0f, 36.0f);
}

/* ------------------------------------------------------------------ modifiers */
/* TOP: the checker and the MODIFIERS title with its underline (like SELECT DIFFICULTY). */
void bb_ui_modifiers_top(C2D_TextBuf tb) {
    bb_view_draw_checker(0x60);
    const float ha = bb_ui_appear(0.0f, 0.4f);
    tx(tb, BB_FACE_HEAD, 40, 200.0f, 92.0f - 14.0f * (1.0f - ha), white(ha), C2D_AlignCenter, "MODIFIERS");
    C2D_DrawRectSolid(200.0f - 90.0f * ha, 146.0f, ZP, 180.0f * ha, 1.0f, rgba(0xFF, 0xFF, 0xFF, 0.6f));
}

/* BOTTOM: the ModifiersMenu rows (gear spinning when enabled, name, value) with the highlighted modifier's description underneath;
 * rows and the back button are touch targets (zones BB_ZONE_LEVEL + i, BB_ZONE_BACK). */
void bb_ui_modifiers(C2D_TextBuf tb, int cursor, float speed, bool no_hud, bool no_beat_bar, int legacy) {
    static const char* const names[4] = { "NOTE SPEED", "NO HUD", "NO BEAT BAR", "LEGACY LAYOUT" };
    static const char* const descs[4] = {
        "Change the speed at which the notes get to the center of the screen. This will not increase or decrease the speed of the song, just how fast the notes move.",
        "Hides the heart, score and combo display while you play.",
        "Hides the beat bar (the hit line and its buttons) while you play.",
        "Hold the 3DS sideways, touch screen on the right: the art stands upright and fills the screen, and the notes fall to the bottom. Off: the usual layout."
    };
    const int rows = legacy >= 0 ? 4 : 3;
    static float ys = -1.0f;
    char buf[96];
    bb_view_draw_checker(0x90);
    const float rh = rows == 4 ? 34.0f : 44.0f, y0 = rows == 4 ? 6.0f : 10.0f;
    float ty = y0 + (float)cursor * rh;
    if (ys < 0.0f || bb_ui_enter_time() < 0.03f) ys = ty;
    bb_ui_smooth(&ys, ty, 20.0f);
    C2D_DrawRectSolid(0, ys - 2.0f, ZP - 0.01f, 320.0f, rh - 4.0f, rgba(0xFF, 0xFF, 0xFF, 0.08f));
    for (int i = 0; i < rows; i++) {
        const float a = bb_ui_appear(0.1f + 0.07f * (float)i, 0.35f);
        const float yy = y0 + (float)i * rh;
        const bool sel = i == cursor;
        const bool on = i == 0 ? speed != 1.0f : (i == 1 ? no_hud : (i == 2 ? no_beat_bar : legacy == 1));
        const float spin = on ? bb_ui_time() * 1.8f : 0.0f;
        gear(30.0f + 30.0f * (1.0f - a), yy + rh * 0.41f, 11.0f, spin, sel ? rgba(0xC0, 0xC0, 0xC8, a) : rgba(0x50, 0x50, 0x58, a));
        const float x = 54.0f + 30.0f * (1.0f - a);
        tx(tb, BB_FACE_BOLD, 21, x, yy + 2.0f, sel ? white(a) : dim(a), C2D_AlignLeft, names[i]);
        if (i == 0) snprintf(buf, sizeof(buf), "x%.1f", (double)speed);
        else snprintf(buf, sizeof(buf), "%s", on ? "ENABLED" : "DISABLED");
        tx(tb, BB_FACE_BOLD, 14, 306.0f, yy + 8.0f, on ? orange(a) : dim(a), C2D_AlignRight, buf);
        bb_ui_zone_add(BB_ZONE_LEVEL + i, 0, yy - 2.0f, 320.0f, rh - 4.0f);
    }
    /* the highlighted modifier's description */
    const float pa = bb_ui_appear(0.2f, 0.35f);
    C2D_DrawRectSolid(0, 144.0f, ZP - 0.01f, 320.0f, 96.0f, rgba(0, 0, 0, 0.55f * pa));
    const char* p = descs[cursor < rows ? cursor : 0];
    float y = 152.0f;
    while (*p && y < 200.0f) {
        int len = (int)strlen(p), cut = len;
        if (bb_font_width(BB_FACE_BODY, 14, p) > 296.0f) {
            int last = -1;
            for (int i = 0; i < len && i < 94; i++) {
                snprintf(buf, sizeof(buf), "%.*s", i + 1, p);
                if (bb_font_width(BB_FACE_BODY, 14, buf) > 296.0f) break;
                if (p[i] == ' ') last = i;
            }
            cut = last > 0 ? last : (len < 94 ? len : 94);
        }
        snprintf(buf, sizeof(buf), "%.*s", cut, p);
        tx(tb, BB_FACE_BODY, 14, 12.0f, y, white(pa), C2D_AlignLeft, buf);
        y += 17.0f;
        p += cut;
        while (*p == ' ') p++;
    }
    bb_art_icon(7, 18.0f, 222.0f, 24.0f, C2D_Color32(255, 255, 255, (u8)(255.0f * pa)), ZI + 0.02f);
    bb_ui_zone_add(BB_ZONE_BACK, 0, 204.0f, 40.0f, 36.0f);
}

/* ------------------------------------------------------------------ settings dialog */
static float g_tz = 0.75f;
static void txd(C2D_TextBuf tb, bb_face_t face, int size, float x, float y, u32 c, u32 align, const char* s) {
    if (bb_font_ready()) bb_font_draw(face, size, x, y, c, align, s, g_tz);
    else bb_view_text(tb, x, y, (float)size / 27.0f, c, align, s, g_tz);
}

/* The dialog frame is the original art (themed_settings_ui_tester.png: speaker cabinets, cables, connectors, scaled to 0.234x);
 * returns the content origin inside its dark interior. Interior on screen: x 83..317, y 28..214. */
/* Settings drawn on the bottom screen (opened from the pause menu): no main-menu backdrop (the caller draws the paused highway),
 * the dialog centred on the 320 px screen, touch zones for every entry. */
void bb_ui_settings_on_bottom(bool on) { g_set_bottom = on; }
static float set_dx(void) { return g_set_bottom ? -40.0f : 0.0f; }

static void settings_frame(C2D_TextBuf tb, const char* title, float *x0, float *y0, float *w) {
    const float dx = set_dx();
    if (!g_set_bottom) main_backdrop(tb, 70, false, -1);
    float a = bb_ui_appear(0.0f, 0.35f);
    C2D_DrawRectSolid(0, 0, 0.56f, 400, 240, rgba(0, 0, 0, (g_set_bottom ? 0.7f : 0.55f) * a));
    float drop = -24.0f * (1.0f - a);
    bb_art_settings_frame(49.5f + dx, 2.0f + drop, (u8)(255.0f * a), ZD + 0.01f);
    txd(tb, BB_FACE_BOLD, 21, 200.0f + dx, 34.0f + drop, white(a), C2D_AlignCenter, title);
    /* round back button over the bottom border */
    bb_art_icon(7, 200.0f + dx, 214.0f + drop, 26.0f, C2D_Color32(255, 255, 255, (u8)(255.0f * a)), ZD + 0.05f);
    bb_ui_zone_add(BB_ZONE_BACK, 200.0f + dx - 18.0f, 196.0f, 36.0f, 36.0f);
    *x0 = 95.0f + dx;
    *y0 = 72.0f + drop;
    *w = 210.0f;
}

void bb_ui_settings_menu(C2D_TextBuf tb, const char* const* names, int n, int cursor) {
    float x0, y0, w;
    settings_frame(tb, "Settings", &x0, &y0, &w);
    float pulse = 0.5f + 0.5f * sinf(bb_ui_time() * 5.0f);
    for (int i = 0; i < n; i++) {
        float a = bb_ui_appear(0.12f + 0.04f * (float)i, 0.3f);
        bool sel = i == cursor;
        float cx, y;
        /* Audio | Gameplay / Controls | Data / Credits, a divider, then Main Menu and Quit Game */
        if (i < 4) { cx = x0 + w * (i % 2 ? 0.75f : 0.25f); y = y0 + (float)(i / 2) * 24.0f; }
        else if (i == 4) { cx = x0 + w * 0.5f; y = y0 + 48.0f; }
        else { cx = x0 + w * 0.5f; y = y0 + 80.0f + (float)(i - 5) * 22.0f; }
        if (i == 5) C2D_DrawRectSolid(x0 + 10.0f, y0 + 75.0f, ZD + 0.02f, w - 20.0f, 1.0f, rgba(0x48, 0x48, 0x52, a));
        float sw = bb_font_ready() ? bb_font_width_smallcaps(BB_FACE_BOLD, sel ? 17 : 14, names[i]) : 40.0f;
        if (sel) C2D_DrawRectSolid(cx - sw * 0.5f - 6.0f, y - 2.0f, ZD + 0.02f, sw + 12.0f, 24.0f, rgba(0xFF, 0xFF, 0xFF, 0.08f + 0.05f * pulse));
        if (bb_font_ready()) bb_font_draw_smallcaps(BB_FACE_BOLD, sel ? 17 : 14, cx, y, sel ? white(a) : dim(a), C2D_AlignCenter, names[i], g_tz);
        else txd(tb, BB_FACE_BOLD, 14, cx, y, sel ? white(a) : dim(a), C2D_AlignCenter, names[i]);
        bb_ui_zone_add(BB_ZONE_LEVEL + i, cx - (i < 4 ? w * 0.25f : w * 0.5f), y - 3.0f, i < 4 ? w * 0.5f : w, i < 4 ? 24.0f : 22.0f);
    }
    footer(tb, "D-Pad: move     A: select     B: back");
}

/* A settings page. slider[i] >= 0 draws a 0..10 slider (Audio, footage 2:44), otherwise label + value with arrows. */
void bb_ui_settings_page(C2D_TextBuf tb, const char* title, const char* const* labels, const char* const* values, const int* slider, int n, int cursor) {
    float x0, y0, w;
    settings_frame(tb, title, &x0, &y0, &w);
    static float row_y = -1.0f;
    float ty = y0 + (float)cursor * 22.0f;
    if (row_y < 0.0f) row_y = ty;
    bb_ui_smooth(&row_y, ty, 22.0f);
    C2D_DrawRectSolid(x0 - 4.0f, row_y - 3.0f, ZD + 0.02f, w + 8.0f, 25.0f, rgba(0xFF, 0xFF, 0xFF, 0.07f + 0.04f * (0.5f + 0.5f * sinf(bb_ui_time() * 5.0f))));
    for (int i = 0; i < n; i++) {
        float a = bb_ui_appear(0.1f + 0.05f * (float)i, 0.3f);
        float y = y0 + (float)i * 22.0f;
        bool sel = i == cursor;
        if (bb_font_ready()) bb_font_draw_smallcaps(BB_FACE_BOLD, sel ? 17 : 14, x0 + 2.0f, y, sel ? white(a) : dim(a), C2D_AlignLeft, labels[i], g_tz);
        else txd(tb, BB_FACE_BOLD, 14, x0 + 2.0f, y, sel ? white(a) : dim(a), C2D_AlignLeft, labels[i]);
        bb_ui_zone_add(BB_ZONE_LEVEL + i, x0 - 4.0f, y - 3.0f, w + 8.0f, 22.0f);
        if (slider && slider[i] >= 0) {
            float sx = x0 + 82.0f, sw = w - 90.0f, f = (float)slider[i] / 10.0f;
            bb_ui_zone_add(BB_ZONE_ACT + i, sx - 6.0f, y - 3.0f, sw + 12.0f, 22.0f);   /* tap / drag on the bar sets the value */
            u32 tc = sel ? white(a) : rgba(0x70, 0x70, 0x7A, a);
            C2D_DrawRectSolid(sx, y + 9.0f, ZD + 0.03f, sw, sel ? 3.0f : 2.0f, rgba(0x40, 0x40, 0x4A, a));
            C2D_DrawRectSolid(sx, y + 9.0f, ZD + 0.031f, sw * f, sel ? 3.0f : 2.0f, tc);
            C2D_DrawCircleSolid(sx + sw * f, y + 10.0f, ZD + 0.032f, sel ? 4.5f : 3.0f, tc);
        } else if (values[i] && values[i][0]) {
            float sway = 2.0f * sinf(bb_ui_time() * 6.0f);
            float vw = bb_font_ready() ? bb_font_width(BB_FACE_BOLD, 14, values[i]) : 40.0f;
            float vx = x0 + w - 8.0f;
            if (sel) {
                txd(tb, BB_FACE_BOLD, 14, vx - vw - 8.0f - sway, y + 1.0f, white(a), C2D_AlignRight, "<");
                txd(tb, BB_FACE_BOLD, 14, vx + 6.0f + sway, y + 1.0f, white(a), C2D_AlignLeft, ">");
            }
            txd(tb, BB_FACE_BOLD, 14, vx, y + 1.0f, sel ? white(a) : dim(a), C2D_AlignRight, values[i]);
        }
    }
    footer(tb, "D-Pad: move / change     L/R: page     B: back");
}

/* Settings > Credits (HotMenu/credits_menu.gd: staff, additional credits, voice cast, community). Role names are the English
 * readings of the original's CREDITS_* locale keys (the English locale table is not in the recovered source). */
typedef struct { uint8_t kind; const char* a; const char* b; } credit_line_t;   /* 0 heading, 1 name + role, 2 note, 3 name (role below) */
static float credit_h(int kind) { return kind == 1 ? 26.0f : (kind == 0 ? 24.0f : (kind == 3 ? 16.0f : 15.0f)); }
static const credit_line_t k_credits[] = {
    { 0, "Beat Banger", NULL },
    { 1, "Ken", "Engineering, Art & Music" },
    { 1, "Oskenso", "Backend Operations" },
    { 1, "Ultrahyper", "Art & Cutscenes" },
    { 1, "Ochi (Oddkong)", "Animation" },
    { 1, "1MP", "Art & UI" },
    { 1, "Valentine", "Art & UI" },
    { 1, "Blue Kobold", "Writing & Operations" },
    { 1, "Kapigator", "Art" },
    { 0, "Additional Credits", NULL },
    { 1, "StriderTempxa", "Additional Music" },
    { 1, "Glitchtherobot", "Additional Programming" },
    { 1, "Free Pancakes", "Additional Concept" },
    { 0, "Voice Cast", NULL },
    { 1, "Bliss Eclair", "Cathy Smith" },
    { 1, "MimiHung VO", "Zoe Monroe" },
    { 1, "TemptyVA", "Gale Shinpen" },
    { 1, "Nami955", "Elaine Claire" },
    { 1, "JellyfishJubilee", "Dawna Mabel" },
    { 1, "Misuzugon", "Makayla Tao" },
    { 0, "Community", NULL },
    { 1, "Newcro, Limejuice", "Discord Moderators" },
    { 3, "DoodleSurface, KirbyKidJ,", NULL },
    { 1, "SockHungryClutz", "Code Snippets" },
    { 0, "Handheld Edition", NULL },
    { 2, "Native port for New Nintendo 3DS / 2DS family systems,", NULL },
    { 2, "built with devkitPro, libctru and citro2d.", NULL },
    { 2, "Thank you for playing!", NULL },
};

void bb_ui_credits(C2D_TextBuf tb, int dir) {
    static float scroll = 0.0f, idle = 0.0f;
    float x0, y0, w;
    settings_frame(tb, "Credits", &x0, &y0, &w);
    const float view = 128.0f, top = y0 - 4.0f;
    float total = 0.0f;
    for (size_t i = 0; i < sizeof(k_credits) / sizeof(k_credits[0]); i++) total += credit_h(k_credits[i].kind);
    const float max_scroll = total - view + 8.0f > 0.0f ? total - view + 8.0f : 0.0f;
    static float last_enter = 1e9f;   /* the page was (re)entered when the enter clock went backwards, even after a long frame */
    if (bb_ui_enter_time() < last_enter) { scroll = -18.0f; idle = 0.0f; }   /* the first heading starts clear of the top fade */
    last_enter = bb_ui_enter_time();
    if (dir != 0) { scroll += 18.0f * (float)dir; idle = 0.0f; }
    else {
        idle += bb_ui_dt();
        if (idle > 1.5f && !bb_ui_reduce_motion()) scroll += 16.0f * bb_ui_dt();   /* rolls slowly by itself, like end credits */
    }
    if (scroll > max_scroll + 40.0f && dir == 0) scroll = -view * 0.6f;               /* loop after a short pause past the end */
    if (scroll < -view * 0.6f) scroll = -view * 0.6f;
    if (dir > 0 && scroll > max_scroll) scroll = max_scroll;
    float y = top - scroll;
    const float a0 = bb_ui_appear(0.15f, 0.35f);
    for (size_t i = 0; i < sizeof(k_credits) / sizeof(k_credits[0]); i++) {
        const credit_line_t* c = &k_credits[i];
        const float h = credit_h(c->kind);
        if (y > top - h * 0.5f && y < top + view - h * 0.5f) {
            float edge = (y - top) / 14.0f;                         /* soft fade at the top and bottom of the window */
            const float e2 = (top + view - h - y) / 14.0f;
            if (e2 < edge) edge = e2;
            const float a = a0 * (edge < 0.0f ? 0.0f : (edge > 1.0f ? 1.0f : edge));
            if (c->kind == 0) {
                if (bb_font_ready()) bb_font_draw_smallcaps(BB_FACE_BOLD, 14, x0 + w * 0.5f, y + 4.0f, orange(a), C2D_AlignCenter, c->a, g_tz);
            } else if (c->kind == 1 || c->kind == 3) {
                txd(tb, BB_FACE_BOLD, 14, x0 + w * 0.5f, y, white(a), C2D_AlignCenter, c->a);
                if (c->b) txd(tb, BB_FACE_BODY, 10, x0 + w * 0.5f, y + 14.0f, dim(a), C2D_AlignCenter, c->b);
            } else {
                txd(tb, BB_FACE_BODY, 10, x0 + w * 0.5f, y, dim(a), C2D_AlignCenter, c->a);
            }
        }
        y += h;
    }
    footer(tb, "D-Pad: scroll     B: back");
}

/* calibration_root.gd: on every beat node (beat % 4) alpha 0.5 -> 1.0 and scale 1.5 -> 1.0 (expo out, 1 s); L / R change the offset. */
void bb_ui_calibration(C2D_TextBuf tb, int ms, int beat, float beat_s) {
    char buf[32];
    (void)tb;
    C2D_DrawRectSolid(0, 0, 0.30f, 400, 240, C2D_Color32(0x0A, 0x0A, 0x10, 0xFF));
    const float a = bb_ui_appear(0.0f, 0.35f);
    tx(tb, BB_FACE_BOLD, 26, 200.0f, 22.0f, white(a), C2D_AlignCenter, "Calibration");
    tx(tb, BB_FACE_BODY, 12, 200.0f, 56.0f, dim(a), C2D_AlignCenter, "Tap along in your head: if the pulse feels late, add latency");
    for (int i = 0; i < 4; i++) {
        const bool on = beat >= 0 && (beat & 3) == i;
        float k = 1.0f;
        float al = 0.5f;
        if (on) {
            al = 1.0f;
            const float t = beat_s > 1.0f ? 1.0f : (beat_s < 0.0f ? 0.0f : beat_s);
            k = 1.0f + 0.5f * (1.0f - (1.0f - powf(2.0f, -10.0f * t)));
        }
        const float cx = 80.0f + 80.0f * (float)i, cy = 118.0f;
        C2D_DrawCircleSolid(cx, cy, ZP, 22.0f * k + 3.0f, rgba(0xFF, 0xFF, 0xFF, 0.30f * al * a));
        C2D_DrawCircleSolid(cx, cy, ZP + 0.01f, 22.0f * k, rgba(0x14, 0x14, 0x1C, al * a));
        C2D_DrawCircleSolid(cx, cy, ZP + 0.02f, 13.0f * k, on ? orange(a) : rgba(0x70, 0x70, 0x7C, al * a));
    }
    snprintf(buf, sizeof(buf), "%+d ms", ms);
    const float sway = bb_ui_reduce_motion() ? 0.0f : 2.0f * sinf(bb_ui_time() * 6.0f);
    tx(tb, BB_FACE_BOLD, 26, 200.0f, 164.0f, white(a), C2D_AlignCenter, buf);
    tx(tb, BB_FACE_BOLD, 26, 118.0f - sway, 164.0f, dim(a), C2D_AlignCenter, "<");
    tx(tb, BB_FACE_BOLD, 26, 282.0f + sway, 164.0f, dim(a), C2D_AlignCenter, ">");
    footer(tb, "D-Pad L/R: adjust     B: back");
}

void bb_ui_vignette(void) {
    const u32 d = C2D_Color32(0, 0, 0, 0x58), c = C2D_Color32(0, 0, 0, 0);
    const float z = 0.94f;
    C2D_DrawRectangle(0, 0, z, 64, 240, d, c, d, c);        /* left   (tl, tr, bl, br) */
    C2D_DrawRectangle(336, 0, z, 64, 240, c, d, c, d);      /* right  */
    C2D_DrawRectangle(0, 0, z, 400, 44, d, d, c, c);        /* top    */
    C2D_DrawRectangle(0, 196, z, 400, 44, c, c, d, d);      /* bottom */
}
