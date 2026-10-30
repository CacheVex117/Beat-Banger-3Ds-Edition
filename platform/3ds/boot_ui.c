/**
 * @file boot_ui.c
 * @brief Boot sequence screens (see boot_ui.h).
 */
#include "boot_ui.h"
#include "menu_ui.h"
#include "game_view.h"
#include "ui_motion.h"
#include "font.h"
#include "theme.h"
#include <math.h>

#define ZB 0.40f
#define ZTX 0.50f
#define ZFADE 0.97f

static C2D_SpriteSheet g_keyart, g_rabbit, g_splash_h;

static u32 rgba(u8 r, u8 g, u8 b, float a) { return C2D_Color32(r, g, b, (u8)(a < 0.0f ? 0.0f : (a > 1.0f ? 255.0f : a * 255.0f))); }

void bb_boot_load(void) {
    if (!g_keyart) g_keyart = C2D_SpriteSheetLoad("romfs:/boot/keyart.t3x");
    if (!g_rabbit) g_rabbit = C2D_SpriteSheetLoad("romfs:/boot/rabbit.t3x");
    if (g_keyart) C3D_TexSetFilter(C2D_SpriteSheetGetImage(g_keyart, 0).tex, GPU_LINEAR, GPU_LINEAR);
    if (g_rabbit) C3D_TexSetFilter(C2D_SpriteSheetGetImage(g_rabbit, 0).tex, GPU_LINEAR, GPU_LINEAR);
    if (bb_theme_halloween() && !g_splash_h) g_splash_h = C2D_SpriteSheetLoad("romfs:/boot/splash_h.t3x");
    if (g_splash_h) C3D_TexSetFilter(C2D_SpriteSheetGetImage(g_splash_h, 0).tex, GPU_LINEAR, GPU_LINEAR);
}

void bb_boot_free(void) {
    if (g_keyart) { C2D_SpriteSheetFree(g_keyart); g_keyart = NULL; }
    if (g_rabbit) { C2D_SpriteSheetFree(g_rabbit); g_rabbit = NULL; }
    if (g_splash_h) { C2D_SpriteSheetFree(g_splash_h); g_splash_h = NULL; }
}

/* Anton with a drop shadow, centred (the disclaimer card's heavy condensed caps) */
static void head(int size, float y, u32 col, const char* s) {
    if (!bb_font_ready()) return;
    bb_font_draw(BB_FACE_HEAD, size, 202.0f, y + 2.0f, rgba(0, 0, 0, 0.75f), C2D_AlignCenter, s, ZTX - 0.01f);
    bb_font_draw(BB_FACE_HEAD, size, 200.0f, y, col, C2D_AlignCenter, s, ZTX);
}

static void head_at(float cx, int size, float y, u32 col, const char* s) {
    if (!bb_font_ready()) return;
    bb_font_draw(BB_FACE_HEAD, size, cx + 1.5f, y + 1.5f, rgba(0, 0, 0, 0.75f), C2D_AlignCenter, s, ZTX - 0.01f);
    bb_font_draw(BB_FACE_HEAD, size, cx, y, col, C2D_AlignCenter, s, ZTX);
}

static void fade(float w, float black) {
    if (black > 0.003f) C2D_DrawRectSolid(0, 0, ZFADE, w, 240, rgba(0, 0, 0, black > 1.0f ? 1.0f : black));
}

static void prompt(float cx, float y, const char* key, const char* label, bool lit) {
    const float pulse = lit ? 0.88f + 0.12f * sinf(bb_ui_time() * 4.0f) : 0.55f;   /* Continue bright and breathing, Exit quieter */
    C2D_DrawCircleSolid(cx - 34.0f, y + 9.0f, ZTX, 8.0f, rgba(0xF4, 0xF4, 0xF8, pulse));
    if (!bb_font_ready()) return;
    bb_font_draw(BB_FACE_BOLD, 12, cx - 34.0f, y + 1.0f, rgba(0x10, 0x10, 0x16, 1.0f), C2D_AlignCenter, key, ZTX + 0.01f);
    bb_font_draw_smallcaps(BB_FACE_BOLD, 17, cx - 22.0f, y - 1.0f, rgba(0xF4, 0xF4, 0xF8, pulse), C2D_AlignLeft, label, ZTX);
}

/* ---------------------------------------------------------------- the Halloween splash (splash_bunny_halloween.tscn "splash", 13 s) */
static float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static float ease_t(float x, float c) {   /* Godot ease(): c < 1 eases out (1 - (1 - x)^(1/c)), 1 is linear */
    x = clamp01(x);
    if (c >= 1.0f) return x;
    float r = 1.0f, b = 1.0f - x;
    for (int i = 0, n = (int)(1.0f / c + 0.5f); i < n; i++) r *= b;
    return 1.0f - r;
}
static float key2(float t, float t0, float t1, float v0, float v1, float c) {   /* two keys, held before / after */
    if (t <= t0) return v0;
    if (t >= t1) return v1;
    return v0 + (v1 - v0) * ease_t((t - t0) / (t1 - t0), c);
}
#define HK (240.0f / 1080.0f)
#define HX(x) ((x) * HK - 13.3f)
static void splash_cell(float sx, float sy, float sw, float sh, float cx, float cy, float w, float h, float angle, float alpha, float z) {
    if (!g_splash_h || alpha <= 0.003f) return;
    C2D_Image img = C2D_SpriteSheetGetImage(g_splash_h, 0);
    Tex3DS_SubTexture st = { (u16)sw, (u16)sh, sx / 512.0f, 1.0f - sy / 512.0f, (sx + sw) / 512.0f, 1.0f - (sy + sh) / 512.0f };
    img.subtex = &st;
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, C2D_Color32(255, 255, 255, (u8)(255.0f * clamp01(alpha))), 0.0f);
    C2D_DrawParams dp = { { cx, cy, w, h }, { w * 0.5f, h * 0.5f }, z, angle };
    C2D_DrawImage(img, &dp, &t);
}

static void halloween_splash(float t) {
    C2D_DrawRectSolid(0, 0, ZB - 0.05f, 400, 240, C2D_Color32(0, 0, 0, 0xFF));
    /* Rain (CPUParticles2D, 200 drops over 2560 px from y -72, falling; visible from 3.4 s, faded 9.2 -> 11.3 s) */
    if (t >= 3.4f) {
        const float ra = 1.0f - key2(t, 9.2f, 11.3f, 0.0f, 1.0f, 1.0f);
        for (int i = 0; i < 70; i++) {
            uint32_t h = (uint32_t)i * 2654435761u;
            const float x = (float)(h % 400u), ph = (float)((h >> 9) % 1000u) / 1000.0f, sc = 1.0f + 2.0f * (float)((h >> 19) % 100u) / 100.0f;
            const float y = fmodf((t - 3.4f) * 520.0f + ph * 300.0f, 300.0f) - 30.0f;
            splash_cell(352.0f, 104.0f, 16.0f, 32.0f, x, y, 16.0f * HK * sc, 32.0f * HK * sc, 0.0f, 0.55f * ra, ZB + 0.01f);
        }
    }
    /* SplashBunNofan (scale 1.38153): modulate 0 -> 1 over 0.567 -> 0.9 s (ease 0.5), out 9.267 -> 11.367 s; it rises 580 -> 516 over 2.6 -> 4.5 s */
    const float bs = 1.38153f;
    const float ba = key2(t, 0.5667f, 0.9f, 0.0f, 1.0f, 0.5f) * (1.0f - key2(t, 9.2667f, 11.3667f, 0.0f, 1.0f, 1.0f));
    const float bx = HX(940.0f), by = key2(t, 2.6f, 4.5f, 580.0f, 516.0f, 0.2f) * HK;
    splash_cell(0.0f, 0.0f, 349.0f, 340.0f, bx, by, 349.0f * bs * HK, 340.0f * bs * HK, 0.0f, ba, ZB + 0.02f);
    /* SplashFan (child at 68.9, 1.1): three turns down to 0 over 0.4 -> 3.07 s (ease 0.2) */
    const float fr = key2(t, 0.4f, 3.0667f, 18.8496f, 0.0f, 0.2f);
    splash_cell(352.0f, 0.0f, 100.0f, 100.0f, bx + 68.9332f * bs * HK, by + 1.12218f * bs * HK, 100.0f * bs * HK, 100.0f * bs * HK, fr, ba, ZB + 0.03f);
    /* SplashText: 650 -> 716 and 0 -> 1 over 2.6 -> 4.5 s (ease 0.2), out 9.2 -> 11.2 s */
    const float ta = key2(t, 2.6f, 4.5f, 0.0f, 1.0f, 0.2f) * (1.0f - key2(t, 9.2f, 11.2f, 0.0f, 1.0f, 1.0f));
    splash_cell(0.0f, 344.0f, 315.0f, 106.0f, HX(960.0f), key2(t, 2.6f, 4.5f, 650.0f, 716.0f, 0.2f) * HK, 315.0f * HK * 1.4f, 106.0f * HK * 1.4f, 0.0f, ta, ZB + 0.04f);
    /* Lightning: a white flash at 3.4 s fading out by 4.37 s */
    const float la = t >= 3.4f ? 1.0f - key2(t, 3.4f, 4.3667f, 0.0f, 1.0f, 1.0f) : 0.0f;
    if (la > 0.003f) C2D_DrawRectSolid(0, 0, ZB + 0.06f, 400, 240, C2D_Color32(255, 255, 255, (u8)(255.0f * la)));
}

void bb_boot_top(int step, float black) {
    if (step == 0) {
        bb_view_draw_checker(0x30);
        head(64, 46.0f, rgba(0xFF, 0xFF, 0xFF, 1.0f), "WARNING");
        head(30, 136.0f, rgba(0xE8, 0x24, 0x2C, 1.0f), "THE FOLLOWING CONTENT IS 18+");
    } else if (step == 1) {   /* the disclaimer card: small caps line, the big word, the ownership paragraph, the plea */
        bb_view_draw_checker(0x30);
        C2D_DrawRectSolid(0, 0, ZB - 0.05f, 400, 240, rgba(0, 0, 0, 0.45f));
        const u32 w = rgba(0xFF, 0xFF, 0xFF, 1.0f);
        head(22, 14.0f, w, "THE FOLLOWING IS A");
        head(52, 40.0f, w, "FANMADE PORT");
        head(18, 112.0f, w, "BEATBANGER IS ALL OWNED BY");
        head(18, 136.0f, w, "BUNFAN STUDIOS");
        if (bb_font_ready()) {   /* the plea, in the lighter weight like the card's last line */
            bb_font_draw(BB_FACE_BOLD, 14, 201.0f, 181.0f, rgba(0, 0, 0, 0.7f), C2D_AlignCenter, "Please support the official release on", ZTX - 0.01f);
            bb_font_draw(BB_FACE_BOLD, 14, 200.0f, 180.0f, w, C2D_AlignCenter, "Please support the official release on", ZTX);
            bb_font_draw(BB_FACE_BOLD, 14, 201.0f, 199.0f, rgba(0, 0, 0, 0.7f), C2D_AlignCenter, "Steam, Itch.io or Patreon.com/cw/bunfan.", ZTX - 0.01f);
            bb_font_draw(BB_FACE_BOLD, 14, 200.0f, 198.0f, w, C2D_AlignCenter, "Steam, Itch.io or Patreon.com/cw/bunfan.", ZTX);
        }
    }
    else if (bb_theme_halloween()) halloween_splash(bb_ui_enter_time());   /* splash.gd: the Halloween bunny instead of the intro video */
    /* step 2 (default theme): main.c draws the intro video */
    fade(400.0f, black);
}

void bb_boot_bottom(int step, float black) {
    if (step == 0) {
        bb_view_draw_checker(0x30);
        head_at(160.0f, 20, 58.0f, rgba(0xFF, 0xFF, 0xFF, 1.0f), "THIS GAME CONTAINS ADULT CONTENT");
        head_at(160.0f, 20, 90.0f, rgba(0xC8, 0xC8, 0xD2, 1.0f), "PLAYER DISCRETION IS ADVISED");
        C2D_DrawRectSolid(60.0f, 128.0f, ZB, 200.0f, 1.0f, rgba(0xFF, 0xFF, 0xFF, 0.35f));
        prompt(92.0f, 172.0f, "A", "Continue", true);
        prompt(242.0f, 172.0f, "B", "Exit", false);
        bb_ui_zone_add(BB_ZONE_LEVEL, 30.0f, 156.0f, 120.0f, 50.0f);
        bb_ui_zone_add(BB_ZONE_BACK, 180.0f, 156.0f, 110.0f, 50.0f);
    } else if (step == 1) {
        C2D_DrawRectSolid(0, 0, ZB - 0.05f, 320, 240, C2D_Color32(0, 0, 0, 0xFF));
        if (g_keyart) {
            C2D_Image img = C2D_SpriteSheetGetImage(g_keyart, 0);
            C2D_DrawImageAt(img, 0.0f, 0.0f, ZB, NULL, 1.0f, 1.0f);
        }
        bb_ui_zone_add(BB_ZONE_LEVEL, 0, 0, 320, 240);   /* a touch anywhere continues */
    } else {
        C2D_DrawRectSolid(0, 0, ZB - 0.05f, 320, 240, C2D_Color32(0, 0, 0, 0xFF));
        if (g_rabbit) {   /* bunfan_icon_rabbit_long_small, dimmed */
            C2D_Image img = C2D_SpriteSheetGetImage(g_rabbit, 0);
            const float s = 0.6f, w = (float)img.subtex->width * s, h = (float)img.subtex->height * s;
            C2D_ImageTint t;
            C2D_PlainImageTint(&t, C2D_Color32(255, 255, 255, 0x60), 0.0f);
            C2D_DrawImageAt(img, 160.0f - w * 0.5f, 120.0f - h * 0.5f, ZB, &t, s, s);
        }
        bb_ui_zone_add(BB_ZONE_LEVEL, 0, 0, 320, 240);
    }
    fade(320.0f, black);
}
