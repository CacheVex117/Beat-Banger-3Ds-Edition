/**
 * @file ui_motion.c
 * @brief UI animation helpers (see ui_motion.h).
 */
#include "ui_motion.h"
#include <citro2d.h>
#include <math.h>

static float g_time, g_enter, g_dt = 1.0f / 60.0f;

void bb_ui_tick(float dt_s) {
    if (dt_s < 0.0f) dt_s = 0.0f;
    if (dt_s > 0.1f) dt_s = 0.1f;   /* stage loads / captures must not teleport animations */
    g_dt = dt_s;
    g_time += dt_s;
    g_enter += dt_s;
}

void  bb_ui_enter(void) { g_enter = 0.0f; }
float bb_ui_time(void) { return g_time; }
float bb_ui_enter_time(void) { return g_enter; }
float bb_ui_dt(void) { return g_dt; }

static float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

float bb_ui_ease_out(float t) {
    t = 1.0f - clamp01(t);
    return 1.0f - t * t * t;
}

float bb_ui_ease_out_back(float t) {
    t = clamp01(t) - 1.0f;
    return 1.0f + t * t * (2.70158f * t + 1.70158f);
}

static bool g_reduce_motion;
void bb_ui_set_reduce_motion(bool on) { g_reduce_motion = on; }
bool bb_ui_reduce_motion(void) { return g_reduce_motion; }

static float g_beats = -1.0f;
void bb_ui_set_beat(float beats) { g_beats = beats; }
float bb_ui_beat_pulse(void) {
    if (g_reduce_motion || g_beats < 0.0f) return 0.0f;
    const float f = g_beats - floorf(g_beats);
    const float k = 1.0f - f;
    return k * k * k;
}

float bb_ui_beat_since(float bpm) {
    if (g_reduce_motion || g_beats < 0.0f || bpm <= 0.0f) return 99.0f;
    return (g_beats - floorf(g_beats)) * 60.0f / bpm;
}

/* ---- touch zones: the bottom-screen draw registers what can be tapped; input reads them on the next frame ---- */
#define ZONE_MAX 48
static struct { int id; float x, y, w, h; } g_zone[ZONE_MAX];
static int g_zone_n;
void bb_ui_zone_clear(void) { g_zone_n = 0; }
void bb_ui_zone_add(int id, float x, float y, float w, float h) {
    if (g_zone_n >= ZONE_MAX) return;
    g_zone[g_zone_n].id = id; g_zone[g_zone_n].x = x; g_zone[g_zone_n].y = y; g_zone[g_zone_n].w = w; g_zone[g_zone_n].h = h;
    g_zone_n++;
}
bool bb_ui_zone_rect(int id, float* x, float* w) {
    for (int i = g_zone_n - 1; i >= 0; i--) if (g_zone[i].id == id) { *x = g_zone[i].x; *w = g_zone[i].w; return true; }
    return false;
}
int bb_ui_zone_hit(int px, int py) {
    for (int i = g_zone_n - 1; i >= 0; i--) {   /* last added = drawn on top */
        if ((float)px >= g_zone[i].x && (float)px < g_zone[i].x + g_zone[i].w && (float)py >= g_zone[i].y && (float)py < g_zone[i].y + g_zone[i].h) return g_zone[i].id;
    }
    return -1;
}

float bb_ui_appear(float delay, float dur) {
    if (g_reduce_motion) return 1.0f;
    return bb_ui_ease_out((g_enter - delay) / dur);
}

float bb_ui_smooth(float* cur, float target, float rate) {
    float k = 1.0f - expf(-rate * g_dt);
    *cur += (target - *cur) * k;
    if (fabsf(target - *cur) < 0.002f) *cur = target;
    return *cur;
}

void bb_ui_draw_fade(void) {
    float a = 1.0f - bb_ui_appear(0.0f, 0.28f);
    if (a > 0.003f) C2D_DrawRectSolid(0, 0, 0.95f, 400, 240, C2D_Color32(0, 0, 0, (u8)(a * 255.0f)));
}
