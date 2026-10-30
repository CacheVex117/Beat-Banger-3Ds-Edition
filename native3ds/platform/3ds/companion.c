/**
 * @file companion.c
 * @brief Bottom-screen companion outside gameplay (see companion.h).
 *
 * Design rules: one dark backdrop with a soft spotlight from the top edge (the main menu's stage lights, breathing on the
 * menu beat), the menus' type (Encode Sans SC Condensed / Anton), the menus' orange accent, and one row of button hints at
 * the bottom. Every screen shows at most one card of information that the top screen does not already show.
 */
#include "companion.h"
#include "font.h"
#include "game_view.h"
#include "input_map.h"
#include "menu_ui.h"
#include "progress.h"
#include "ui_art.h"
#include "ui_motion.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define W   320.0f
#define H   240.0f
#define Z0  0.10f
#define ZP  0.30f
#define ZT  0.50f

static u32 rgba(u8 r, u8 g, u8 b, float a) { return C2D_Color32(r, g, b, (u8)(a < 0.0f ? 0.0f : (a > 1.0f ? 255.0f : a * 255.0f))); }
static u32 white(float a)  { return rgba(0xF4, 0xF4, 0xF8, a); }
static u32 orange(float a) { return rgba(0xFF, 0x8A, 0x1E, a); }

static void T(bb_face_t face, int size, float x, float y, u32 c, u32 align, const char* s) {
    if (bb_font_ready()) bb_font_draw(face, size, x, y, c, align, s, ZT);   /* no atlas: the companion simply stays quiet */
}
static float TW(bb_face_t face, int size, const char* s) { return bb_font_ready() ? bb_font_width(face, size, s) : 6.0f * (float)strlen(s); }

/* ------------------------------------------------------------------ backdrop and hints */
static void backdrop(void) {
    C2D_DrawRectSolid(0, 0, Z0, W, H, C2D_Color32(0x0B, 0x0B, 0x11, 0xFF));
    /* a soft spotlight falling from the top edge; it breathes with the menu music's beat like the top screen's logo */
    const float bp = 1.0f + 0.5f * bb_ui_beat_pulse();
    for (int k = 0; k < 5; k++) {
        float r = 70.0f + 34.0f * (float)k;
        C2D_DrawCircleSolid(W * 0.5f, -36.0f, Z0 + 0.01f + 0.001f * (float)k, r, rgba(0xB8, 0xB0, 0xD8, 0.022f * bp));
    }
    C2D_DrawRectSolid(0, 0, Z0 + 0.02f, W, 1.0f, orange(0.55f));
}

static float hint_width(const char* key, const char* label) {
    float kw = strlen(key) <= 1 ? 14.0f : TW(BB_FACE_BOLD, 10, key) + 10.0f;
    return kw + 4.0f + TW(BB_FACE_BODY, 12, label);
}

static void hints(const bb_comp_t* c) {
    if (c->nhints <= 0) return;
    const float a = bb_ui_appear(0.2f, 0.3f);
    C2D_DrawRectSolid(0, 216.0f, ZP, W, 24.0f, rgba(0, 0, 0, 0.55f));
    C2D_DrawRectSolid(0, 216.0f, ZP + 0.01f, W, 1.0f, rgba(0xFF, 0xFF, 0xFF, 0.06f));
    float total = 0.0f;
    for (int i = 0; i < c->nhints; i++) total += hint_width(c->key[i], c->label[i]) + (i ? 16.0f : 0.0f);
    float x = W * 0.5f - total * 0.5f;
    const float cy = 228.0f;
    for (int i = 0; i < c->nhints; i++) {
        const char* k = c->key[i];
        const bool round = strlen(k) <= 1;
        const float kw = round ? 14.0f : TW(BB_FACE_BOLD, 10, k) + 10.0f;
        const u32 kc = rgba(0xE6, 0xE6, 0xEE, a);
        if (round) {
            C2D_DrawCircleSolid(x + 7.0f, cy, ZP + 0.02f, 7.0f, kc);
        } else {   /* pill for L/R, START, D-Pad ... */
            C2D_DrawRectSolid(x + 5.0f, cy - 6.0f, ZP + 0.02f, kw - 10.0f, 12.0f, kc);
            C2D_DrawCircleSolid(x + 5.0f, cy, ZP + 0.02f, 6.0f, kc);
            C2D_DrawCircleSolid(x + kw - 5.0f, cy, ZP + 0.02f, 6.0f, kc);
        }
        T(BB_FACE_BOLD, 10, x + kw * 0.5f, cy - 7.0f, rgba(0x10, 0x10, 0x16, a), C2D_AlignCenter, k);
        T(BB_FACE_BODY, 12, x + kw + 4.0f, cy - 8.0f, rgba(0xC8, 0xC8, 0xD8, a), C2D_AlignLeft, c->label[i]);
        x += hint_width(k, c->label[i]) + 16.0f;
    }
}

/* ------------------------------------------------------------------ shared pieces */
/* The four receptors exactly as the highway orders them, labelled with what presses each lane in the current control mode. */
/* ------------------------------------------------------------------ screens */
static void draw_help(const bb_comp_t* c) {
    if (!c->help || !c->help[0]) return;
    /* wrap into at most four centred lines */
    char lines[4][96];
    int n = 0;
    const char* p = c->help;
    while (*p && n < 4) {
        while (*p == ' ') p++;
        int len = (int)strlen(p), cut = len;
        if (TW(BB_FACE_BOLD, 14, p) > 270.0f) {
            int last_space = -1;
            for (int i = 0; i < len && i < 94; i++) {
                char tmp[96];
                snprintf(tmp, sizeof(tmp), "%.*s", i + 1, p);
                if (TW(BB_FACE_BOLD, 14, tmp) > 270.0f) break;
                if (p[i] == ' ') last_space = i;
            }
            cut = last_space > 0 ? last_space : (len < 94 ? len : 94);
        }
        snprintf(lines[n++], sizeof(lines[0]), "%.*s", cut, p);
        p += cut;
    }
    static const char* last = NULL;
    static float changed = -10.0f;
    if (c->help != last) { last = c->help; changed = bb_ui_time(); }
    const float e = (bb_ui_reduce_motion() ? 1.0f : bb_ui_ease_out((bb_ui_time() - changed) / 0.22f)) * bb_ui_appear(0.1f, 0.3f);
    const float y0 = 108.0f - (float)n * 10.0f;
    C2D_DrawRectSolid(W * 0.5f - 14.0f, y0 - 14.0f, ZP + 0.02f, 28.0f, 2.0f, orange(e));
    for (int i = 0; i < n; i++) T(BB_FACE_BOLD, 14, W * 0.5f, y0 + (float)i * 20.0f + 5.0f * (1.0f - e), white(e), C2D_AlignCenter, lines[i]);
}

/* ------------------------------------------------------------------ API */
void bb_comp_clear(bb_comp_t* c, bb_comp_kind_t kind) {
    memset(c, 0, sizeof(*c));
    c->kind = kind;
    c->speed = 1.0f;
}

void bb_comp_hint(bb_comp_t* c, const char* key, const char* label) {
    if (c->nhints >= 4) return;
    c->key[c->nhints] = key;
    c->label[c->nhints] = label;
    c->nhints++;
}

void bb_comp_draw(const bb_comp_t* c) {
    backdrop();
    switch (c->kind) {
        case BB_COMP_TITLE:
            break;   /* the top screen carries the title; the bottom stays a quiet stage */
        case BB_COMP_HELP:     draw_help(c); break;
        default: break;
    }
    hints(c);
}
