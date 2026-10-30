/**
 * @file ui_art.c
 * @brief Menu artwork loader/drawer (see ui_art.h).
 */
#include "ui_art.h"
#include "theme.h"
#include "progress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MAP 64

static C2D_SpriteSheet g_diff_icons, g_powerups;
static char g_di_id[32][32];
static int  g_di_diff[32], g_di_cell[32], g_di_n;
static C2D_SpriteSheet g_hw_game, g_hw_levels, g_bg_shop_h;   /* Halloween skins (theme.h), loaded on first use */
static C2D_SpriteSheet g_logo, g_bg[2], g_thumbs, g_icons, g_stars, g_cats, g_ui_icons, g_ach;
static C2D_SpriteSheet g_mm[8];
static C2D_SpriteSheet g_frame;
static int g_mm_fps, g_mm_frames, g_mm_pages, g_mm_cw = 320, g_mm_ch = 240, g_mm_cols = 3, g_mm_rows = 4;
static int g_mm_crop_x = 480, g_mm_crop_y = 0, g_mm_crop_w = 1440;   /* where the 320x240 cells sit in the 1920x1080 video */
static int g_mm_variant = 0, g_mm_want = 0;   /* 0 mm_* (default loop), 1 mmn_* (easter egg), 2 mmh_* (Halloween); one set resident */
static int g_mm_base = 0;                        /* the theme's loop: 0 default, 2 Halloween (background_video.gd HALLOWEEN_BACKGROUND) */
static C2D_SpriteSheet g_theme_icon;
static const char* const k_mm_tag[3] = { "mm", "mmn", "mmh" };
static char g_ach_name[32][24];
static int g_ach_cell[32], g_ach_n;
static char g_thumb_id[MAX_MAP][32];
static int  g_thumb_cell[MAX_MAP], g_thumb_n;
static int  g_icon_item[MAX_MAP], g_icon_cell[MAX_MAP], g_icon_n;

static C2D_SpriteSheet load(const char* name) {
    char path[64];
    snprintf(path, sizeof(path), "romfs:/ui/%s", name);
    return C2D_SpriteSheetLoad(path);
}

static void free_sheet(C2D_SpriteSheet* s) {
    if (*s) {
        C2D_SpriteSheetFree(*s);
        *s = NULL;
    }
}

/* Load one main-menu loop set (frees the other first: 8 x 0.5 MiB pages, so only one set is ever resident). */
static void mm_load(int variant) {
    for (int i = 0; i < 8; i++) free_sheet(&g_mm[i]);
    g_mm_frames = 0;
    char nm[24];
    snprintf(nm, sizeof(nm), "romfs:/ui/%s.txt", k_mm_tag[variant]);
    FILE* mf = fopen(nm, "r");
    int nf = mf ? fscanf(mf, "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d", &g_mm_fps, &g_mm_frames, &g_mm_cw, &g_mm_ch, &g_mm_cols, &g_mm_rows, &g_mm_pages,
                         &g_mm_crop_x, &g_mm_crop_y, &g_mm_crop_w) : 0;
    if (nf >= 7 && g_mm_pages <= 8) {
        for (int i = 0; i < g_mm_pages; i++) {
            snprintf(nm, sizeof(nm), "%s_%d.t3x", k_mm_tag[variant], i);
            g_mm[i] = load(nm);
            if (!g_mm[i]) { g_mm_frames = 0; break; }
        }
    } else {
        g_mm_frames = 0;
    }
    if (mf) fclose(mf);
    g_mm_variant = g_mm_want = variant;
    if (g_mm_frames <= 0 && variant) mm_load(0);   /* the alternative loop is missing: keep the default one */
}

/* Calendar.GAME_THEME: the Halloween logo (logo.gd %Halloween) and menu loop, or the default ones. Called before the boot sequence
 * and when the Change Theme button restarts the game; the loop swap happens inside the next bb_art_mainmenu draw. */
void bb_art_set_theme(bool halloween) {
    free_sheet(&g_logo);
    free_sheet(&g_theme_icon);
    if (halloween) g_logo = load("logo_h.t3x");
    if (!g_logo) g_logo = load("logo.t3x");
    g_mm_base = halloween ? 2 : 0;
    g_mm_want = g_mm_base;
}

void bb_art_theme_icon(float cx, float cy, float size, u32 tint, float z) {
    if (!g_theme_icon) g_theme_icon = load("theme_icon.t3x");
    if (!g_theme_icon) return;
    C2D_Image img = C2D_SpriteSheetGetImage(g_theme_icon, 0);
    C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, tint, 1.0f);
    C2D_SetTintMode(C2D_TintMult);
    C2D_DrawParams dp = { { cx - size * 0.5f, cy - size * 0.5f, size, size }, { 0, 0 }, z, 0.0f };
    C2D_DrawImage(img, &dp, &t);
}

bool bb_art_init(void) {
    if (!g_logo) g_logo = load("logo.t3x");   /* bb_art_set_theme may already have loaded the Halloween one */
    g_bg[BB_BG_SHOP] = load("shop_bg.t3x");
    g_bg[BB_BG_EXTRAS] = load("extras_bg.t3x");
    g_thumbs = load("thumbs.t3x");
    g_icons = load("shop_icons.t3x");
    g_stars = load("stars.t3x");
    g_cats = load("cats.t3x");
    g_ui_icons = load("icons.t3x");
    g_ach = load("ach_icons.t3x");
    g_frame = load("settings_frame.t3x");
    g_diff_icons = load("diff_icons.t3x");
    g_powerups = load("powerups.t3x");
    {
        FILE* df = fopen("romfs:/ui/diff_icons.txt", "r");
        char l3[96];
        g_di_n = 0;
        while (df && g_di_n < 32 && fgets(l3, sizeof(l3), df)) {
            char* a = strchr(l3, '|');
            if (!a) continue;
            *a = '\0';
            char* b = strchr(a + 1, '|');
            if (!b) continue;
            snprintf(g_di_id[g_di_n], sizeof(g_di_id[0]), "%.31s", l3);
            g_di_diff[g_di_n] = atoi(a + 1);
            g_di_cell[g_di_n++] = atoi(b + 1);
        }
        if (df) fclose(df);
    }
    {
        mm_load(g_mm_base);
        FILE* af = fopen("romfs:/ui/ach_icons.txt", "r");
        char l2[64];
        g_ach_n = 0;
        while (af && g_ach_n < 32 && fgets(l2, sizeof(l2), af)) {
            char* b = strchr(l2, '|');
            if (!b) continue;
            *b = '\0';
            snprintf(g_ach_name[g_ach_n], sizeof(g_ach_name[0]), "%.23s", l2);
            g_ach_cell[g_ach_n++] = atoi(b + 1);
        }
        if (af) fclose(af);
    }

    FILE* f = fopen("romfs:/ui/thumbs.txt", "r");
    char line[96];
    g_thumb_n = 0;
    while (f && g_thumb_n < MAX_MAP && fgets(line, sizeof(line), f)) {
        char* bar = strchr(line, '|');
        if (!bar) continue;
        *bar = '\0';
        snprintf(g_thumb_id[g_thumb_n], sizeof(g_thumb_id[0]), "%.31s", line);
        g_thumb_cell[g_thumb_n++] = atoi(bar + 1);
    }
    if (f) fclose(f);
    f = fopen("romfs:/ui/shop_icons.txt", "r");
    g_icon_n = 0;
    while (f && g_icon_n < MAX_MAP && fgets(line, sizeof(line), f)) {
        char* bar = strchr(line, '|');
        if (!bar) continue;
        g_icon_item[g_icon_n] = atoi(line);
        g_icon_cell[g_icon_n++] = atoi(bar + 1);
    }
    if (f) fclose(f);
    return g_logo != NULL;
}

void bb_art_shutdown(void) {
    free_sheet(&g_logo);
    free_sheet(&g_bg[0]);
    free_sheet(&g_bg[1]);
    free_sheet(&g_bg_shop_h);
    free_sheet(&g_hw_game);
    free_sheet(&g_hw_levels);
    free_sheet(&g_thumbs);
    free_sheet(&g_icons);
    free_sheet(&g_stars);
    free_sheet(&g_diff_icons);
    free_sheet(&g_powerups);
    free_sheet(&g_cats);
    free_sheet(&g_ui_icons);
    free_sheet(&g_ach);
    free_sheet(&g_frame);
    for (int i = 0; i < 8; i++) free_sheet(&g_mm[i]);
}

/* Draw the (sx,sy,sw,sh) texel rectangle of a sheet's first image into (x,y,w,h) (top-left origin). */
/* shop_background.gd: the Halloween room in the Halloween theme */
static C2D_SpriteSheet bg_sheet(int which) {
    if (which == BB_BG_SHOP && bb_theme_halloween()) {
        if (!g_bg_shop_h) g_bg_shop_h = load("shop_bg_h.t3x");
        if (g_bg_shop_h) return g_bg_shop_h;
    }
    return g_bg[which];
}

static void draw_rect(C2D_SpriteSheet sh, float sx, float sy, float sw, float sh_, float x, float y, float w, float h, u32 tint, float z) {
    if (!sh) {
        return;
    }
    C2D_Image img = C2D_SpriteSheetGetImage(sh, 0);
    float tw = (float)img.tex->width, th = (float)img.tex->height;
    Tex3DS_SubTexture st;
    st.width = (u16)sw;
    st.height = (u16)sh_;
    st.left = sx / tw;
    st.right = (sx + sw) / tw;
    st.top = 1.0f - sy / th;
    st.bottom = 1.0f - (sy + sh_) / th;
    img.subtex = &st;
    C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
    C2D_DrawParams dp;
    dp.pos.x = x; dp.pos.y = y; dp.pos.w = w; dp.pos.h = h;
    dp.center.x = 0.0f; dp.center.y = 0.0f; dp.depth = z; dp.angle = 0.0f;
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, tint, 1.0f);
    C2D_SetTintMode(C2D_TintMult);   /* modulate (the default mode would flood the image with the tint colour) */
    C2D_DrawImage(img, &dp, tint == 0xFFFFFFFFu ? NULL : &t);
}

void bb_art_logo(float x, float y, float w, float z) {
    draw_rect(g_logo, 0, 0, 350, 200, x, y, w, w * 200.0f / 350.0f, 0xFFFFFFFFu, z);
}

void bb_art_background(bb_bg_t which, u8 brightness, float z) {
    draw_rect(bg_sheet(which), 0, 0, 400, 240, 0, 0, 400, 240, C2D_Color32(brightness, brightness, brightness, 0xFF), z);
}

void bb_art_background_crop(bb_bg_t which, u8 brightness, float src_x, float src_w, float z) {
    draw_rect(bg_sheet(which), src_x, 0, src_w, 240, 0, 0, src_w, 240, C2D_Color32(brightness, brightness, brightness, 0xFF), z);
}

void bb_art_background_view(bb_bg_t which, u8 brightness, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, float z) {
    draw_rect(bg_sheet(which), sx, sy, sw, sh, dx, dy, dw, dh, C2D_Color32(brightness, brightness, brightness, 0xFF), z);
}

void bb_art_background_part(bb_bg_t which, u8 brightness, float sx, float sy, float sw, float sh, float z) {
    draw_rect(bg_sheet(which), sx, sy, sw, sh, sx, sy, sw, sh, C2D_Color32(brightness, brightness, brightness, 0xFF), z);
}

void bb_art_thumb(const char* stage_id, float cx, float cy, float d, u32 tint, float z) {
    for (int i = 0; i < g_thumb_n; i++) {
        if (strcmp(g_thumb_id[i], stage_id) == 0) {
            int c = g_thumb_cell[i];
            draw_rect(g_thumbs, (float)((c % 4) * 96), (float)((c / 4) * 96), 96, 96, cx - d * 0.5f, cy - d * 0.5f, d, d, tint, z);
            return;
        }
    }
}

void bb_art_star(bool outline, float cx, float cy, float size, u32 tint, float z) {
    draw_rect(g_stars, outline ? 64.0f : 0.0f, 0, 64, 64, cx - size * 0.5f, cy - size * 0.5f, size, size, tint, z);
}

void bb_art_star_rot(bool outline, float cx, float cy, float size, float angle, u32 tint, float z) {
    if (!g_stars) return;
    C2D_Image img = C2D_SpriteSheetGetImage(g_stars, 0);
    Tex3DS_SubTexture st;
    const float tw = (float)img.tex->width, th = (float)img.tex->height, sx = outline ? 64.0f : 0.0f;
    st.width = 64; st.height = 64;
    st.left = sx / tw; st.right = (sx + 64.0f) / tw; st.top = 1.0f; st.bottom = 1.0f - 64.0f / th;
    img.subtex = &st;
    C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
    C2D_DrawParams dp;
    dp.pos.x = cx; dp.pos.y = cy; dp.pos.w = size; dp.pos.h = size;
    dp.center.x = size * 0.5f; dp.center.y = size * 0.5f; dp.depth = z; dp.angle = angle;
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, tint, 1.0f);
    C2D_SetTintMode(C2D_TintMult);
    C2D_DrawImage(img, &dp, &t);
}

void bb_art_category(int idx, float cx, float cy, float size, u32 tint, float z) {
    if (idx < 0 || idx > 3) return;
    draw_rect(g_cats, (float)(idx * 32), 0, 32, 32, cx - size * 0.5f, cy - size * 0.5f, size, size, tint, z);
}

void bb_art_shop_icon(int item, int category_idx, float cx, float cy, float size, u32 tint, float z) {
    for (int i = 0; i < g_icon_n; i++) {
        if (g_icon_item[i] == item) {
            int c = g_icon_cell[i];
            draw_rect(g_icons, (float)((c % 8) * 48), (float)((c / 8) * 48), 48, 48, cx - size * 0.5f, cy - size * 0.5f, size, size, tint, z);
            return;
        }
    }
    bb_art_category(category_idx, cx, cy, size, tint, z);
}

bool bb_art_mainmenu_ready(void) { return g_mm_frames > 0; }

void bb_art_mainmenu_crop(float* x, float* y, float* w) { *x = (float)g_mm_crop_x; *y = (float)g_mm_crop_y; *w = (float)g_mm_crop_w; }

bool bb_art_diff_icon(const char* stage_id, int diff, float cx, float cy, float size, u32 tint, float z) {
    for (int i = 0; i < g_di_n; i++) {
        if (g_di_diff[i] == diff && strcmp(g_di_id[i], stage_id) == 0) {
            const int c = g_di_cell[i];
            draw_rect(g_diff_icons, (float)((c % 8) * 64), (float)((c / 8) * 64), 64, 64, cx - size * 0.5f, cy - size * 0.5f, size, size, tint, z);
            return true;
        }
    }
    return false;
}

void bb_art_powerup(int idx, float cx, float cy, float size, u32 tint, float z) {
    if (idx < 0 || idx > 5) return;
    draw_rect(g_powerups, (float)((idx % 5) * 96), (float)((idx / 5) * 96), 96, 96, cx - size * 0.5f, cy - size * 0.5f, size, size, tint, z);
}

void bb_art_mainmenu_variant(int variant) { g_mm_want = variant ? 1 : g_mm_base; }   /* 0 = the theme's own loop */
int  bb_art_mainmenu_current(void) { return g_mm_variant; }

void bb_art_mainmenu(float t_s, float x, float y, u8 brightness, float z) {
    if (g_mm_want != g_mm_variant) mm_load(g_mm_want);   /* drawing happens after C3D_FrameBegin (SYNCDRAW): the GPU is done with the old pages */
    if (g_mm_frames <= 0) {
        return;
    }
    const int per = g_mm_cols * g_mm_rows;
    int f = (int)(t_s * (float)g_mm_fps) % g_mm_frames;
    int page = f / per, c = f % per;
    draw_rect(g_mm[page], (float)((c % g_mm_cols) * g_mm_cw), (float)((c / g_mm_cols) * g_mm_ch), (float)g_mm_cw, (float)g_mm_ch, x, y,
              (float)g_mm_cw, (float)g_mm_ch, C2D_Color32(brightness, brightness, brightness, 0xFF), z);
}

void bb_art_icon(int idx, float cx, float cy, float size, u32 tint, float z) {
    if (idx < 0 || idx > 7) return;
    draw_rect(g_ui_icons, (float)(idx * 32), 0, 32, 32, cx - size * 0.5f, cy - size * 0.5f, size, size, tint, z);
}

void bb_art_ach_icon(const char* name, float cx, float cy, float size, u32 tint, float z) {
    int cell = 0;
    for (int i = 0; i < g_ach_n; i++) if (strcmp(g_ach_name[i], name) == 0) cell = g_ach_cell[i];
    draw_rect(g_ach, (float)((cell % 4) * 64), (float)((cell / 4) * 64), 64, 64, cx - size * 0.5f, cy - size * 0.5f, size, size, tint, z);
}

void bb_art_ach(int ach_id, float cx, float cy, float size, u32 tint, float z) {
    const bb_ach_def_t* d = bb_ach_find(ach_id);
    const float k = d && d->icon_pct ? (float)d->icon_pct / 100.0f : 1.0f;
    bb_art_ach_icon(bb_ach_icon_name(ach_id), cx, cy, size * k, tint, z);
}

void bb_art_settings_frame(float x, float y, u8 alpha, float z) {
    draw_rect(g_frame, 0, 0, 301, 228, x, y, 301, 228, C2D_Color32(255, 255, 255, alpha), z);
}

/* ---------------- Halloween skins (hw_game.t3x / hw_levels.t3x, tools/build_theme.py) ---------------- */
static void hw_draw(C2D_SpriteSheet sh, float sx, float sy, float sw, float sh_, float cx, float cy, float w, float h, float angle, u32 tint, float z) {
    if (!sh) return;
    C2D_Image img = C2D_SpriteSheetGetImage(sh, 0);
    Tex3DS_SubTexture st;
    const float tw = (float)img.tex->width, th = (float)img.tex->height;
    st.width = (u16)sw; st.height = (u16)sh_;
    st.left = sx / tw; st.right = (sx + sw) / tw; st.top = 1.0f - sy / th; st.bottom = 1.0f - (sy + sh_) / th;
    if (w < 0.0f) { const float l = st.left; st.left = st.right; st.right = l; w = -w; }   /* a negative width mirrors */
    img.subtex = &st;
    C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, tint, 0.0f);
    C2D_SetTintMode(C2D_TintMult);
    C2D_DrawParams dp = { { cx, cy, w, h }, { w * 0.5f, h * 0.5f }, z, angle };
    C2D_DrawImage(img, &dp, &t);
}

void bb_art_hw_cell(int cell, float cx, float cy, float size, float angle, u32 tint, float z) {
    if (!g_hw_game) g_hw_game = load("hw_game.t3x");
    hw_draw(g_hw_game, (float)cell * 64.0f, 0.0f, 64.0f, 64.0f, cx, cy, size, size, angle, tint, z);
}

void bb_art_hw_web(float cx, float cy, float w, float h, u32 tint, float z) {
    if (!g_hw_game) g_hw_game = load("hw_game.t3x");
    hw_draw(g_hw_game, 0.0f, 64.0f, 400.0f, 190.0f, cx, cy, w, h, 0.0f, tint, z);
}

void bb_art_hw_level_deco(int k, float cx, float cy, float scale, float angle, u32 tint, float z) {
    static const float r[4][4] = { { 0, 0, 116, 203 }, { 120, 0, 116, 251 }, { 250, 0, 79, 67 }, { 250, 70, 74, 67 } };
    if (k < 0 || k > 3) return;
    if (!g_hw_levels) g_hw_levels = load("hw_levels.t3x");
    hw_draw(g_hw_levels, r[k][0], r[k][1], r[k][2], r[k][3], cx, cy, r[k][2] * scale, r[k][3] * scale, angle, tint, z);
}
