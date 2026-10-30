/**
 * @file font.c
 * @brief Bitmap UI font renderer (see font.h).
 */
#include "font.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIZES 8
#define GLYPHS 95   /* ASCII 32..126 */

typedef struct { int16_t x, y, w, h, xo, yo, adv; } glyph_t;
typedef struct { int size, ascent, line; glyph_t g[GLYPHS]; } fsize_t;
typedef struct { int count; fsize_t s[MAX_SIZES]; } fface_t;

static fface_t g_face[7];
static C2D_SpriteSheet g_sheet, g_sheet_ol;   /* g_sheet: a8 coverage (tinted solid); g_sheet_ol: RGBA glyphs with a baked outline (tinted mult) */
static bool g_ready;

static int face_index(const char* n) {
    if (strcmp(n, "body") == 0) return BB_FACE_BODY;
    if (strcmp(n, "bold") == 0) return BB_FACE_BOLD;
    if (strcmp(n, "head") == 0) return BB_FACE_HEAD;
    if (strcmp(n, "hand") == 0) return BB_FACE_HAND;
    if (strcmp(n, "noto") == 0) return BB_FACE_NOTO;
    if (strcmp(n, "notob") == 0) return BB_FACE_NOTO_BLACK;
    if (strcmp(n, "notor") == 0) return BB_FACE_NOTO_REG;
    return -1;
}

static fsize_t* find_size(fface_t* f, int size, bool create) {
    for (int i = 0; i < f->count; i++) if (f->s[i].size == size) return &f->s[i];
    if (!create || f->count >= MAX_SIZES) return NULL;
    fsize_t* s = &f->s[f->count++];
    memset(s, 0, sizeof(*s));
    s->size = size;
    return s;
}

bool bb_font_init(void) {
    memset(g_face, 0, sizeof(g_face));
    g_sheet = C2D_SpriteSheetLoad("romfs:/ui/font.t3x");
    g_sheet_ol = C2D_SpriteSheetLoad("romfs:/ui/font_ol.t3x");
    FILE* f = fopen("romfs:/ui/font.txt", "r");
    if (!g_sheet || !f) {
        if (f) fclose(f);
        g_ready = false;
        return false;
    }
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        char face[8];
        int size, a, b, code, x, y, w, h, xo, yo, adv;
        if (line[0] == 'F' && sscanf(line, "F|%7[^|]|%d|%d|%d", face, &size, &a, &b) == 4) {
            int fi = face_index(face);
            if (fi < 0) continue;
            fsize_t* s = find_size(&g_face[fi], size, true);
            if (s) { s->ascent = a; s->line = b; }
        } else if (line[0] == 'G' && sscanf(line, "G|%7[^|]|%d|%d|%d|%d|%d|%d|%d|%d|%d", face, &size, &code, &x, &y, &w, &h, &xo, &yo, &adv) == 10) {
            int fi = face_index(face);
            if (fi < 0 || code < 32 || code > 126) continue;
            fsize_t* s = find_size(&g_face[fi], size, false);
            if (!s) continue;
            glyph_t* g = &s->g[code - 32];
            g->x = (int16_t)x; g->y = (int16_t)y; g->w = (int16_t)w; g->h = (int16_t)h;
            g->xo = (int16_t)xo; g->yo = (int16_t)yo; g->adv = (int16_t)adv;
        }
    }
    fclose(f);
    g_ready = g_face[0].count > 0 && g_face[1].count > 0 && g_face[2].count > 0;
    return g_ready;
}

void bb_font_shutdown(void) {
    if (g_sheet) {
        C2D_SpriteSheetFree(g_sheet);
        g_sheet = NULL;
    }
    if (g_sheet_ol) {
        C2D_SpriteSheetFree(g_sheet_ol);
        g_sheet_ol = NULL;
    }
    g_ready = false;
}

bool bb_font_ready(void) { return g_ready; }

static const fsize_t* nearest(bb_face_t face, int size) {
    if (face == BB_FACE_NOTO && (!g_sheet_ol || g_face[BB_FACE_NOTO].count == 0)) face = BB_FACE_BOLD;   /* no outlined atlas: plain bold */
    if ((face == BB_FACE_NOTO_BLACK || face == BB_FACE_NOTO_REG) && g_face[face].count == 0) face = face == BB_FACE_NOTO_BLACK ? BB_FACE_BOLD : BB_FACE_BODY;
    const fface_t* f = &g_face[face];
    const fsize_t* best = &f->s[0];
    for (int i = 0; i < f->count; i++) {
        if (abs(f->s[i].size - size) < abs(best->size - size)) best = &f->s[i];
    }
    return best;
}

void bb_font_from_scale(float scale, bb_face_t* face, int* size) {
    int px = (int)lroundf(scale * 27.0f);
    if (scale >= 0.95f) *face = BB_FACE_HEAD;
    else if (px < 12) *face = BB_FACE_BODY;
    else *face = BB_FACE_BOLD;
    *size = nearest(*face, px)->size;
}

float bb_font_width(bb_face_t face, int size, const char* s) {
    const fsize_t* fs = nearest(face, size);
    float w = 0.0f;
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        w += (float)fs->g[c - 32].adv;
    }
    return w;
}

int bb_font_line_height(bb_face_t face, int size) { return nearest(face, size)->line; }

static C2D_SpriteSheet g_run_sheet;   /* the sheet draw_run reads (set per face) */
static void draw_run(const fsize_t* fs, float x, float y, u32 color, const char* s, float z) {
    C2D_Image img = C2D_SpriteSheetGetImage(g_run_sheet ? g_run_sheet : g_sheet, 0);
    const float tw = (float)img.tex->width, th = (float)img.tex->height;
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, color, 1.0f);
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        const glyph_t* g = &fs->g[c - 32];
        if (g->w > 0) {
            Tex3DS_SubTexture st;
            st.width = (u16)g->w;
            st.height = (u16)g->h;
            st.left = (float)g->x / tw;
            st.right = (float)(g->x + g->w) / tw;
            st.top = 1.0f - (float)g->y / th;
            st.bottom = 1.0f - (float)(g->y + g->h) / th;
            img.subtex = &st;
            C2D_DrawParams dp;
            dp.pos.x = floorf(x) + (float)g->xo;
            dp.pos.y = floorf(y) + (float)g->yo;
            dp.pos.w = (float)g->w;
            dp.pos.h = (float)g->h;
            dp.center.x = 0.0f; dp.center.y = 0.0f; dp.depth = z; dp.angle = 0.0f;
            C2D_DrawImage(img, &dp, &tint);
        }
        x += (float)g->adv;
    }
}

void bb_font_draw(bb_face_t face, int size, float x, float y, u32 color, u32 align, const char* s, float z) {
    if (!g_ready || !s || !s[0]) return;
    const fsize_t* fs = nearest(face, size);
    float w = bb_font_width(face, fs->size, s);
    if (align & C2D_AlignRight) x -= w;
    else if (align & C2D_AlignCenter) x -= w * 0.5f;
    if (face == BB_FACE_NOTO && g_sheet_ol && g_face[BB_FACE_NOTO].count > 0) {   /* outlined HUD face: white fill x colour, black outline kept */
        g_run_sheet = g_sheet_ol;
        C3D_TexSetFilter(C2D_SpriteSheetGetImage(g_sheet_ol, 0).tex, GPU_NEAREST, GPU_NEAREST);
        C2D_SetTintMode(C2D_TintMult);
        draw_run(fs, x, y, color, s, z);
        g_run_sheet = NULL;
        return;
    }
    C3D_TexSetFilter(C2D_SpriteSheetGetImage(g_sheet, 0).tex, GPU_NEAREST, GPU_NEAREST);   /* 1:1 pixels */
    C2D_SetTintMode(C2D_TintSolid);                                                      /* glyphs are white coverage: tint = text colour */
    if (fs->size >= 17) draw_run(fs, x + 1.0f, y + 1.0f, C2D_Color32(0, 0, 0, (u8)((color >> 24) * 0.75f)), s, z);
    draw_run(fs, x, y, color, s, z + 0.005f);
    C2D_SetTintMode(C2D_TintMult);
}

/* ---- small caps ---- */
static int small_size(bb_face_t face, int size) { return nearest(face, (int)lroundf((float)size * 0.8f))->size; }

float bb_font_width_smallcaps(bb_face_t face, int size, const char* s) {
    int sm = small_size(face, size);
    float w = 0.0f;
    bool word_start = true;
    char one[2] = { 0, 0 };
    for (; *s; s++) {
        one[0] = word_start ? *s : (char)((*s >= 'a' && *s <= 'z') ? *s - 32 : *s);
        w += bb_font_width(face, word_start ? size : sm, one);
        word_start = *s == ' ';
    }
    return w;
}

void bb_font_draw_smallcaps(bb_face_t face, int size, float x, float y, u32 color, u32 align, const char* s, float z) {
    if (!g_ready || !s || !s[0]) return;
    int sm = small_size(face, size);
    float w = bb_font_width_smallcaps(face, size, s);
    if (align & C2D_AlignRight) x -= w;
    else if (align & C2D_AlignCenter) x -= w * 0.5f;
    const fsize_t* big = nearest(face, size);
    const fsize_t* small = nearest(face, sm);
    float base_y = y + (float)big->ascent;                 /* shared baseline */
    bool word_start = true;
    char one[2] = { 0, 0 };
    C3D_TexSetFilter(C2D_SpriteSheetGetImage(g_sheet, 0).tex, GPU_NEAREST, GPU_NEAREST);
    C2D_SetTintMode(C2D_TintSolid);
    for (; *s; s++) {
        const fsize_t* fs = word_start ? big : small;
        one[0] = word_start ? *s : (char)((*s >= 'a' && *s <= 'z') ? *s - 32 : *s);
        float yy = base_y - (float)fs->ascent;
        if (fs->size >= 17) draw_run(fs, x + 1.0f, yy + 1.0f, C2D_Color32(0, 0, 0, (u8)((color >> 24) * 0.75f)), one, z);
        draw_run(fs, x, yy, color, one, z + 0.005f);
        x += bb_font_width(face, fs->size, one);
        word_start = *s == ' ';
    }
    C2D_SetTintMode(C2D_TintMult);
}

void bb_font_draw_xf(bb_face_t face, int size, float cx, float cy, float scale, float angle, u32 color, const char* s, float z) {
    if (!g_ready || !s || !s[0]) return;
    const fsize_t* fs = nearest(face, size);
    float w = bb_font_width(face, fs->size, s) * scale;
    float lh = (float)fs->line * scale;
    float ca = cosf(angle), sa = sinf(angle);
    const bool ol = face == BB_FACE_NOTO && g_sheet_ol && g_face[BB_FACE_NOTO].count > 0;
    C2D_Image img = C2D_SpriteSheetGetImage(ol ? g_sheet_ol : g_sheet, 0);
    const float tw = (float)img.tex->width, th = (float)img.tex->height;
    C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
    C2D_SetTintMode(ol ? C2D_TintMult : C2D_TintSolid);
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, color, 1.0f);
    float pen = -w * 0.5f;
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        const glyph_t* g = &fs->g[c - 32];
        if (g->w > 0) {
            float lx = pen + (float)g->xo * scale, ly = -lh * 0.5f + (float)g->yo * scale;   /* glyph top-left relative to the string centre */
            Tex3DS_SubTexture st;
            st.width = (u16)g->w; st.height = (u16)g->h;
            st.left = (float)g->x / tw; st.right = (float)(g->x + g->w) / tw;
            st.top = 1.0f - (float)g->y / th; st.bottom = 1.0f - (float)(g->y + g->h) / th;
            img.subtex = &st;
            C2D_DrawParams dp;
            dp.pos.x = cx + lx * ca - ly * sa;
            dp.pos.y = cy + lx * sa + ly * ca;
            dp.pos.w = (float)g->w * scale; dp.pos.h = (float)g->h * scale;
            dp.center.x = 0.0f; dp.center.y = 0.0f; dp.depth = z; dp.angle = angle;
            C2D_DrawImage(img, &dp, &tint);
        }
        pen += (float)g->adv * scale;
    }
    C2D_SetTintMode(C2D_TintMult);
}

void bb_font_draw_rot(bb_face_t face, int size, float cx, float cy, float angle, u32 color, const char* s, float z) {
    bb_font_draw_xf(face, size, cx, cy, 1.0f, angle, color, s, z);
}
