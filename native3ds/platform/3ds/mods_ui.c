/**
 * @file mods_ui.c
 * @brief Main menu > Mods, bottom screen (see mods_ui.h).
 */
#include "mods_ui.h"
#include "menu_ui.h"
#include "game_view.h"
#include "ui_art.h"
#include "ui_motion.h"
#include "font.h"
#include "mod_gauge.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define ZP 0.35f
#define ZI 0.45f
#define ZT 0.50f
#define MAX_MODS 64

static u32 rgba(u8 r, u8 g, u8 b, float a) { return C2D_Color32(r, g, b, (u8)(a < 0.0f ? 0.0f : (a > 1.0f ? 255.0f : a * 255.0f))); }
static u32 white(float a)  { return rgba(0xF4, 0xF4, 0xF8, a); }
static u32 dim(float a)    { return rgba(0x86, 0x86, 0x92, a); }
static u32 orange(float a) { return rgba(0xFF, 0x8A, 0x1E, a); }


static C2D_SpriteSheet g_thumb[MAX_MODS];
static int g_thumb_n = 0;


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

void bb_mods_open(const bb_mod_entry_t* e, int n) {
    bb_mods_close();
    g_thumb_n = n < MAX_MODS ? n : MAX_MODS;
    for (int i = 0; i < g_thumb_n; i++) {
        g_thumb[i] = e[i].thumb_path ? C2D_SpriteSheetLoad(e[i].thumb_path) : NULL;
        if (g_thumb[i]) C3D_TexSetFilter(C2D_SpriteSheetGetImage(g_thumb[i], 0).tex, GPU_LINEAR, GPU_LINEAR);
    }
}

void bb_mods_close(void) {
    for (int i = 0; i < g_thumb_n; i++) if (g_thumb[i]) { C2D_SpriteSheetFree(g_thumb[i]); g_thumb[i] = NULL; }
    g_thumb_n = 0;
}

/* A colour per pack name, so the pack pills group the list at a glance */
static void pack_color(const char* s, u8* r, u8* g, u8* b) {
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    static const u8 pal[6][3] = { { 0xE8, 0x5A, 0x8A }, { 0x5A, 0x9A, 0xE8 }, { 0x8A, 0x5A, 0xE8 }, { 0xE8, 0x9A, 0x3A }, { 0x4A, 0xC0, 0x8A }, { 0xD0, 0x5A, 0x5A } };
    *r = pal[h % 6][0]; *g = pal[h % 6][1]; *b = pal[h % 6][2];
}

/* The round portrait: white ring, black ring, the (pre-masked) thumbnail; a lettered disc when the mod has none. */
static void portrait(int i, const char* name, float cx, float cy, float r, float a, float z) {
    C2D_DrawCircleSolid(cx, cy, z, r, white(a));
    C2D_DrawCircleSolid(cx, cy, z + 0.001f, r * 0.9f, rgba(0, 0, 0, a));
    const float ir = r * 0.8f;
    if (i >= 0 && i < g_thumb_n && g_thumb[i]) {
        C2D_Image img = C2D_SpriteSheetGetImage(g_thumb[i], 0);
        C2D_ImageTint t;
        C2D_PlainImageTint(&t, C2D_Color32(255, 255, 255, (u8)(255.0f * a)), 0.0f);
        C2D_DrawParams dp = { { cx - ir, cy - ir, ir * 2.0f, ir * 2.0f }, { 0, 0 }, z + 0.002f, 0.0f };
        C2D_DrawImage(img, &dp, &t);
    } else {
        u8 cr, cg, cb;
        pack_color(name ? name : "?", &cr, &cg, &cb);
        C2D_DrawCircleSolid(cx, cy, z + 0.002f, ir, rgba(cr, cg, cb, a));
        char ch[2] = { name && name[0] ? name[0] : '?', 0 };
        if (bb_font_ready()) bb_font_draw(BB_FACE_HEAD, (int)(ir * 1.2f), cx, cy - ir * 0.78f, white(a), C2D_AlignCenter, ch, z + 0.003f);
    }
}

static void mod_ribbon(float x, float y, float h, float a, float z) {
    const float w = h * 3.3f, sl = h * 0.35f;
    const u32 c = rgba(0x2C, 0x1C, 0xD8, a), c2 = rgba(0x56, 0x48, 0xF0, a);
    C2D_DrawTriangle(x, y, c, x + w, y, c, x + w - sl, y + h * 0.5f, c, z);
    C2D_DrawTriangle(x, y, c, x + w - sl, y + h * 0.5f, c, x + w, y + h, c, z);
    C2D_DrawTriangle(x, y, c, x + w, y + h, c, x, y + h, c, z);
    C2D_DrawRectSolid(x + w * 0.55f, y, z + 0.001f, w * 0.18f, h, c2);   /* the lighter fold */
    if (bb_font_ready()) bb_font_draw(BB_FACE_BOLD, (int)(h * 0.85f), x + w * 0.42f, y + h * 0.02f, white(a), C2D_AlignCenter, "MOD", z + 0.002f);
}

static void back_btn(float cx, float cy, float a) {
    bb_art_icon(7, cx, cy, 26.0f, C2D_Color32(255, 255, 255, (u8)(255.0f * a)), ZT);
    bb_ui_zone_add(BB_ZONE_BACK, cx - 18.0f, cy - 18.0f, 36.0f, 36.0f);
}

static u32 gauge_color(bb_mod_gauge_t g, float a) {
    static const u8 c[4][3] = { { 0x4A, 0xC0, 0x8A }, { 0xF0, 0xC8, 0x3C }, { 0xFF, 0x8A, 0x1E }, { 0xE8, 0x40, 0x40 } };
    return rgba(c[g][0], c[g][1], c[g][2], a);
}

/* Mod level counter + limit gauge along the bottom of the hub: "MOD LEVELS  31/56  Good" over a bar with ticks at 1/2 and 3/4. */
static void limit_gauge(int used, int cap, float a) {
    const bb_mod_gauge_t g = bb_mod_gauge(used, cap);
    const float x = 52.0f, w = 254.0f, y = 220.0f, h = 7.0f;
    float fill = cap > 0 ? (float)used / (float)cap : 1.0f;
    if (fill > 1.0f) fill = 1.0f;
    fill *= bb_ui_ease_out(bb_ui_appear(0.15f, 0.6f));   /* the bar fills on entry */
    rounded(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, 4.5f, rgba(0, 0, 0, 0.7f * a), ZP);
    rounded(x, y, w, h, 3.5f, rgba(0x26, 0x24, 0x30, a), ZP + 0.01f);
    if (fill > 0.0f) rounded(x, y, w * fill < h ? h : w * fill, h, 3.5f, gauge_color(g, a), ZP + 0.02f);
    for (int k = 2; k <= 3; k++) C2D_DrawRectSolid(x + w * (float)k / 4.0f, y - 2.0f, ZP + 0.03f, 1.0f, h + 4.0f, rgba(0xFF, 0xFF, 0xFF, 0.35f * a));
    if (bb_font_ready()) {
        char buf[24];
        bb_font_draw_smallcaps(BB_FACE_BOLD, 12, x, y - 17.0f, dim(a), C2D_AlignLeft, "Mod levels", ZT);
        snprintf(buf, sizeof(buf), "%d/%d", used, cap);
        const float sw = bb_font_width(BB_FACE_BOLD, 12, bb_mod_gauge_name(g));
        bb_font_draw(BB_FACE_BOLD, 12, x + w - sw - 8.0f, y - 17.0f, white(a), C2D_AlignRight, buf, ZT);
        bb_font_draw(BB_FACE_BOLD, 12, x + w, y - 17.0f, gauge_color(g, a), C2D_AlignRight, bb_mod_gauge_name(g), ZT);
    }
}

/* ------------------------------------------------------------------ hub */
void bb_mods_hub_bottom(int cursor, int mod_count, int used, int cap) {
    bb_view_draw_checker(0x60);
    const float a = bb_ui_appear(0.0f, 0.35f);
    if (bb_font_ready()) {
        bb_font_draw(BB_FACE_HEAD, 34, 161.0f, 7.0f, rgba(0, 0, 0, 0.6f * a), C2D_AlignCenter, "MODS", ZT - 0.01f);
        bb_font_draw(BB_FACE_HEAD, 34, 160.0f, 5.0f, white(a), C2D_AlignCenter, "MODS", ZT);
    }
    C2D_DrawRectSolid(160.0f - 60.0f * a, 50.0f, ZP, 120.0f * a, 1.0f, rgba(0xFF, 0xFF, 0xFF, 0.6f));
    static const char* const lab[2] = { "Library", "Modifiers" };
    char sub[2][40];
    snprintf(sub[0], sizeof(sub[0]), mod_count == 1 ? "1 level" : "%d levels", mod_count);
    snprintf(sub[1], sizeof(sub[1]), "Speed, HUD, beat bar");
    const float pulse = 0.5f + 0.5f * sinf(bb_ui_time() * 5.0f);
    for (int i = 0; i < 2; i++) {
        const float ca = bb_ui_appear(0.06f + 0.08f * (float)i, 0.35f);
        const bool sel = i == cursor;
        const float w = 132.0f, h = 128.0f, x = 22.0f + (float)i * 144.0f, y = 66.0f + 18.0f * (1.0f - bb_ui_ease_out_back(ca)) - (sel ? 3.0f : 0.0f);
        rounded(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, 9.0f, sel ? white(ca * (0.85f + 0.15f * pulse)) : white(0.7f * ca), ZP);   /* 2 px white outline */
        rounded(x, y, w, h, 8.0f, rgba(0x0C, 0x0B, 0x12, 0.95f * ca), ZP + 0.01f);
        const float cx = x + w * 0.5f, iy = y + 44.0f;
        if (i == 0) {   /* a fanned stack of level cards */
            for (int k = 2; k >= 0; k--) {
                const float ox = (float)(k - 1) * 13.0f, oy = (float)k * -3.0f;
                rounded(cx - 17.0f + ox, iy - 24.0f + oy, 34.0f, 46.0f, 4.0f, white(ca * (k == 0 ? 1.0f : 0.55f)), ZP + 0.02f + 0.002f * (float)(2 - k));
                rounded(cx - 15.0f + ox, iy - 22.0f + oy, 30.0f, 42.0f, 3.0f, k == 0 ? orange(ca) : rgba(0x30, 0x2C, 0x40, ca), ZP + 0.021f + 0.002f * (float)(2 - k));
            }
            mod_ribbon(cx + 2.0f, iy + 8.0f, 11.0f, ca, ZI);
        } else {
            bb_art_ach(1015, cx, iy, 52.0f, C2D_Color32(255, 255, 255, (u8)(255.0f * ca)), ZI);   /* the gear (Something Different) */
        }
        if (bb_font_ready()) {
            bb_font_draw_smallcaps(BB_FACE_BOLD, 21, cx, y + 80.0f, sel ? white(ca) : dim(ca), C2D_AlignCenter, lab[i], ZT);
            bb_font_draw(BB_FACE_BODY, 10, cx, y + 106.0f, sel ? orange(ca) : rgba(0x70, 0x70, 0x7C, ca), C2D_AlignCenter, sub[i], ZT);
        }
        bb_ui_zone_add(BB_ZONE_LEVEL + i, x, y, w, h);
    }
    back_btn(22.0f, 216.0f, a);
    limit_gauge(used, cap, bb_ui_appear(0.12f, 0.35f));
}

/* ------------------------------------------------------------------ library */
static void small_stars(float x, float y, int grade, float a) {
    const int lit = grade <= 0 ? 0 : (grade > 5 ? 5 : grade);
    for (int k = 0; k < 5; k++) bb_art_star(k >= lit, x + (float)k * 11.0f, y, 10.0f, k < lit ? C2D_Color32(0xFF, 0xC8, 0x3C, (u8)(255.0f * a)) : C2D_Color32(0x70, 0x70, 0x7C, (u8)(200.0f * a)), ZI);
}

void bb_mods_library_bottom(const bb_mod_entry_t* e, int n, int cursor, int used, int cap) {
    bb_view_draw_checker(0x60);
    const float a = bb_ui_appear(0.0f, 0.3f);
    C2D_DrawRectangle(0, 0, ZP - 0.02f, 320, 34.0f, rgba(0, 0, 0, 0.75f), rgba(0, 0, 0, 0.75f), rgba(0, 0, 0, 0.0f), rgba(0, 0, 0, 0.0f));
    if (bb_font_ready()) {
        bb_font_draw_smallcaps(BB_FACE_BOLD, 21, 48.0f - 20.0f * (1.0f - a), 5.0f, white(a), C2D_AlignLeft, "Library", ZT);
        char buf[24];
        snprintf(buf, sizeof(buf), "%d/%d", used, cap);
        bb_font_draw(BB_FACE_BOLD, 12, 310.0f, 10.0f, gauge_color(bb_mod_gauge(used, cap), a), C2D_AlignRight, buf, ZT);
    }
    back_btn(22.0f, 17.0f, a);
    if (n <= 0) {
        if (bb_font_ready()) {
            bb_font_draw(BB_FACE_BOLD, 14, 160.0f, 96.0f, white(a), C2D_AlignCenter, "No mods installed", ZT);
            bb_font_draw(BB_FACE_BODY, 10, 160.0f, 118.0f, dim(a), C2D_AlignCenter, "Copy MOD2DS output to sdmc:/3ds/bb_mods/", ZT);
        }
        return;
    }
    const float rh = 46.0f, y0 = 40.0f;
    const int vis = 4;
    static float scroll = 0.0f;
    float target = (float)(cursor - 1);
    if (target > (float)(n - vis)) target = (float)(n - vis);
    if (target < 0.0f) target = 0.0f;
    if (bb_ui_enter_time() < 0.03f) scroll = target;
    bb_ui_smooth(&scroll, target, 14.0f);
    const float pulse = 0.5f + 0.5f * sinf(bb_ui_time() * 5.0f);
    for (int i = 0; i < n; i++) {
        const float y = y0 + ((float)i - scroll) * rh;
        if (y < y0 - rh * 0.6f || y > y0 + rh * ((float)vis - 0.4f)) continue;
        const float ra = bb_ui_appear(0.05f + 0.04f * (float)(i % vis), 0.3f);
        const bool sel = i == cursor;
        const float x = 8.0f + 30.0f * (1.0f - ra) + (sel ? 4.0f : 0.0f), w = 304.0f - (sel ? 4.0f : 0.0f);
        if (sel) rounded(x - 1.5f, y - 1.5f, w + 3.0f, rh - 3.0f, 7.0f, white(ra * (0.6f + 0.4f * pulse)), ZP);
        rounded(x, y, w, rh - 6.0f, 6.0f, sel ? rgba(0x1C, 0x18, 0x26, 0.96f * ra) : rgba(0x0A, 0x09, 0x10, 0.88f * ra), ZP + 0.01f);
        if (sel) C2D_DrawRectSolid(x, y + 6.0f, ZP + 0.02f, 3.0f, rh - 18.0f, orange(ra));
        portrait(i, e[i].character, x + 22.0f, y + (rh - 6.0f) * 0.5f, 17.0f, ra, ZI);
        u8 pr, pg, pb;
        pack_color(e[i].pack, &pr, &pg, &pb);
        if (bb_font_ready()) {
            char t[48];
            snprintf(t, sizeof(t), "%.30s", e[i].title);
            bb_font_draw(BB_FACE_BOLD, 14, x + 46.0f, y + 2.0f, sel ? white(ra) : rgba(0xD8, 0xD8, 0xE0, ra), C2D_AlignLeft, t, ZT);
            snprintf(t, sizeof(t), "%.26s", e[i].character);
            bb_font_draw(BB_FACE_BODY, 10, x + 46.0f, y + 22.0f, dim(ra), C2D_AlignLeft, t, ZT);
            /* pack pill */
            snprintf(t, sizeof(t), "%.18s", e[i].pack);
            const float pw = bb_font_width(BB_FACE_BODY, 10, t) + 10.0f;
            rounded(x + w - pw - 8.0f, y + 4.0f, pw, 13.0f, 6.0f, rgba(pr, pg, pb, 0.85f * ra), ZP + 0.02f);
            bb_font_draw(BB_FACE_BODY, 10, x + w - pw * 0.5f - 8.0f, y + 4.0f, white(ra), C2D_AlignCenter, t, ZT);
        }
        small_stars(x + w - 58.0f, y + 29.0f, e[i].best_grade, ra);
        bb_ui_zone_add(BB_ZONE_LEVEL + i, x, y, w, rh - 6.0f);
    }
    if (n > vis) {   /* scroll bar */
        const float th = (float)vis * rh * (float)vis / (float)n, ty = y0 + ((float)vis * rh - th) * scroll / (float)(n - vis);
        C2D_DrawRectSolid(316.0f, ty, ZP + 0.03f, 2.0f, th, rgba(0xFF, 0xFF, 0xFF, 0.45f));
    }
}

/* ------------------------------------------------------------------ a mod's screen (the original's mod level card) */
void bb_mods_detail_bottom(const bb_mod_entry_t* e, int index, bool armed) {
    bb_view_draw_checker(0x60);
    C2D_DrawRectSolid(0, 0, ZP - 0.03f, 320, 240, rgba(0x0C, 0x02, 0x0A, 0.55f));
    const float a = bb_ui_appear(0.0f, 0.4f);
    if (bb_font_ready()) {   /* title (Anton, drop shadow) and the character under it, top left */
        char t[48];
        snprintf(t, sizeof(t), "%.40s", e->title);
        int size = 30;
        while (size > 18 && bb_font_width(BB_FACE_HEAD, size, t) > 296.0f) size -= 2;
        const float tx = 12.0f - 24.0f * (1.0f - a);
        bb_font_draw(BB_FACE_HEAD, size, tx + 2.0f, 5.0f, rgba(0, 0, 0, 0.7f * a), C2D_AlignLeft, t, ZT - 0.01f);
        bb_font_draw(BB_FACE_HEAD, size, tx, 3.0f, white(a), C2D_AlignLeft, t, ZT);
        bb_font_draw(BB_FACE_HEAD, 14, tx, 10.0f + (float)size * 1.35f, white(a), C2D_AlignLeft, e->character, ZT);
        snprintf(t, sizeof(t), "%.30s", e->pack);
        bb_font_draw(BB_FACE_BODY, 10, tx, 30.0f + (float)size * 1.35f, dim(a), C2D_AlignLeft, t, ZT);
    }
    /* the round portrait, pulsing on the preview's beat, with the MOD ribbon; tap it (or A) to choose a difficulty */
    const float pa = bb_ui_ease_out_back(bb_ui_appear(0.05f, 0.45f));
    const float r = 46.0f * (0.6f + 0.4f * pa) * (1.0f + 0.04f * bb_ui_beat_pulse()), cx = 160.0f, cy = 122.0f;
    C2D_DrawCircleSolid(cx + 3.0f, cy + 4.0f, ZI - 0.01f, r, rgba(0, 0, 0, 0.5f * a));   /* shadow */
    portrait(index, e->character, cx, cy, r, a, ZI);
    mod_ribbon(cx + r * 0.42f, cy + r * 0.48f, 13.0f, a, ZI + 0.01f);
    if (armed) C2D_DrawCircleSolid(cx, cy, ZI - 0.005f, r + 4.0f + 1.5f * sinf(bb_ui_time() * 6.0f), orange(0.85f * a));
    bb_ui_zone_add(BB_ZONE_LEVEL, cx - r, cy - r, r * 2.0f, r * 2.0f);
    if (e->best_grade > 0 || e->best_score > 0) {
        small_stars(cx - 22.0f, cy + r + 12.0f, e->best_grade, a);
        if (bb_font_ready()) {
            char b[24];
            snprintf(b, sizeof(b), "%06ld", e->best_score);
            bb_font_draw(BB_FACE_BOLD, 10, cx, cy + r + 18.0f, rgba(0xFF, 0xC8, 0x3C, a), C2D_AlignCenter, b, ZT);
        }
    }
    /* the sawtooth band along the bottom, with the options this mod has */
    const float by = 200.0f;
    C2D_DrawRectSolid(0, by, ZP + 0.05f, 320, 240.0f - by, rgba(0, 0, 0, 0.92f));
    for (int k = 0; k < 20; k++) {
        const float x = (float)k * 16.0f;
        C2D_DrawTriangle(x, by, rgba(0, 0, 0, 0.92f), x + 16.0f, by, rgba(0, 0, 0, 0.92f), x + 8.0f, by - 7.0f, rgba(0, 0, 0, 0.92f), ZP + 0.05f);
    }
    if (bb_font_ready()) {
        bb_font_draw(BB_FACE_BOLD, 12, 160.0f, by + 12.0f, armed ? orange(a) : white(0.6f + 0.4f * sinf(bb_ui_time() * 3.0f)), C2D_AlignCenter, "A  Select Difficulty", ZT + 0.05f);
        bb_font_draw_smallcaps(BB_FACE_BOLD, 14, 306.0f, by + 12.0f, white(a), C2D_AlignRight, "Modifiers >", ZT + 0.05f);
        if (e->has_cutscenes) bb_font_draw(BB_FACE_BODY, 10, 160.0f, by + 27.0f, dim(a), C2D_AlignCenter, "with cutscenes", ZT + 0.05f);
    }
    bb_ui_zone_add(BB_ZONE_MODS, 222.0f, by, 98.0f, 40.0f);
    bb_ui_zone_add(BB_ZONE_LEVEL, 100.0f, by, 120.0f, 40.0f);
    back_btn(22.0f, 220.0f, a);

}
