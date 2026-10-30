/**
 * @file flow_ui.c
 * @brief Results (bottom screen, the original ScoreScreen, with its reward banners; the top dims), pause / count-in / game-over overlays (top). See flow_ui.h.
 */
#include "flow_ui.h"
#include "theme.h"
#include "font.h"
#include "ui_art.h"
#include "ui_motion.h"
#include "ui_sfx.h"
#include "game_view.h"
#include "progress.h"
#include "menu_ui.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define ZO 0.70f   /* above the scene (game_view Z_OVER) */

static u32 rgba(u8 r, u8 g, u8 b, float a) { return C2D_Color32(r, g, b, (u8)(a < 0.0f ? 0.0f : (a > 1.0f ? 255.0f : a * 255.0f))); }
static u32 white(float a) { return rgba(0xF4, 0xF4, 0xF8, a); }

static void T(bb_face_t face, int size, float x, float y, u32 c, u32 align, const char* s) {
    bb_font_draw(face, size, x, y, c, align, s, ZO + 0.10f);
}


static void rrect(float x, float y, float w, float h, float r, u32 c, float z) {
    C2D_DrawRectSolid(x + r, y, z, w - 2.0f * r, h, c);
    C2D_DrawRectSolid(x, y + r, z, r, h - 2.0f * r, c);
    C2D_DrawRectSolid(x + w - r, y + r, z, r, h - 2.0f * r, c);
    C2D_DrawCircleSolid(x + r, y + r, z, r, c);
    C2D_DrawCircleSolid(x + w - r, y + r, z, r, c);
    C2D_DrawCircleSolid(x + r, y + h - r, z, r, c);
    C2D_DrawCircleSolid(x + w - r, y + h - r, z, r, c);
}

/* ---------------------------------------------------------------- results (bottom screen) */
/* The original ScoreScreen (scenes/Game/score_screen.tscn + ScoreScreen.gd, GradeStar.gd, ScoreStatBar.gd) mapped from its 1920x1080
 * canvas onto the 320x240 bottom screen (positions x 1/6 around the centre, y x 0.222; type enlarged for the small screen).
 * "show_stars" timeline: 0-1 s black backing, stars / score / line / personal best ease in (stats fade 0.3-1.3 s); 2 s stars fill one by
 * one over 2 s (sfx_hit_stab, pitch 1 + 0.1 i), score and stat bars count up over 2 s (sfx_score_inc); 4 s white flash, Continue / Retry,
 * stars hover (or "break" below 70 % accuracy) with the cheering / low-stars sound. */
static int64_t g_last_ms = 0;
static uint32_t g_done = 0;   /* bit mask of sounds already played */

#define RS_CX 160.0f
static float rs_x(float x1080) { return RS_CX + (x1080 - 960.0f) * (240.0f / 1080.0f); }
static float rs_y(float y1080) { return y1080 * (240.0f / 1080.0f); }

/* time at which grade star i fills: tween_method(set_stars, 0, grade - 1, 2.0) started at 2 s */
static float star_ms(int i, int grade_n) { return grade_n <= 1 ? 2000.0f : 2000.0f + 2000.0f * (float)i / (float)(grade_n - 1); }

void bb_results_begin(void) {
    g_last_ms = 0;
    g_done = 0;
    bb_uisfx_stop_long();
}

/* Reward banners (the original reports them through its notification popups): payout / EXP, rank-up, then achievements and
 * unlocks. Each drops in on the bottom screen after the white flash, stays ~2 s and leaves; the next follows. */
#define TOAST_T0    5000   /* after the WhiteFlash */
#define TOAST_EACH  2300   /* drop in 250 ms, hold, rise out 250 ms, a short gap */
#define TOAST_SHOW  2050
typedef struct { const char* line1; const char* line2; int kind; char buf[48]; } toast_t;   /* kind -1 payout/rank, 0 achievement, 1 unlock */

static int toast_list(const bb_results_t* r, toast_t* t) {
    int n = 0;
    if (r->rewards) {
        t[n].line1 = r->new_best ? "NEW PERSONAL BEST" : "LEVEL CLEARED";
        snprintf(t[n].buf, sizeof(t[0].buf), "+$%ld   +%ld EXP", (long)r->money, (long)r->exp_gain);
        t[n].line2 = t[n].buf;
        t[n++].kind = -1;
        if (r->rank_after > r->rank_before) {
            t[n].line1 = "RANK UP!";
            snprintf(t[n].buf, sizeof(t[0].buf), "%.40s", bb_rank_title(r->rank_after));
            t[n].line2 = t[n].buf;
            t[n++].kind = -1;
        }
    }
    for (int k = 0; k < r->toast_n && k < 4; k++) {
        t[n].line1 = r->toast_kind[k] ? "UNLOCKED" : "ACHIEVEMENT";
        t[n].line2 = r->toast[k];
        t[n++].kind = r->toast_kind[k];
    }
    return n;
}

void bb_results_update(const bb_results_t* r, int64_t ms) {
    bool jump = ms - g_last_ms > 350;   /* the player skipped ahead: mark what was passed as played, stay silent */
    g_last_ms = ms;
    struct { int64_t t; bb_uisfx_t id; float pitch; } ev[24];
    int n = 0;
    ev[n].t = 0; ev[n].pitch = 1.0f; ev[n++].id = BB_UISFX_REVERSE;                  /* _on_level_passed */
    ev[n].t = 2000; ev[n].pitch = 1.0f; ev[n++].id = BB_UISFX_SCORE;                 /* run_score */
    for (int k = 0; k < r->grade_n && k < 7; k++) { ev[n].t = (int64_t)star_ms(k, r->grade_n); ev[n].pitch = 1.0f + 0.1f * (float)k; ev[n++].id = BB_UISFX_HIT_STAB; }
    ev[n].t = 4000; ev[n].pitch = 1.0f; ev[n++].id = r->accuracy < 70.0f ? BB_UISFX_LOW : BB_UISFX_CHEER;   /* _score_animation_done */
    toast_t tl[6];
    const int tn = toast_list(r, tl);
    for (int k = 0; k < tn; k++) {
        if (tl[k].kind < 0) continue;
        ev[n].t = TOAST_T0 + TOAST_EACH * k; ev[n].pitch = 1.0f; ev[n++].id = tl[k].kind ? BB_UISFX_UNLOCK : BB_UISFX_ACH;
    }
    for (int i = 0; i < n; i++) {
        if (ms >= ev[i].t && !(g_done & (1u << i))) {
            g_done |= 1u << i;
            if (!jump || ev[i].t >= 4000) bb_uisfx_play_pitch(ev[i].id, ev[i].pitch);   /* a skip still lands on the cheer */
        }
    }
}

/* GradeStar particles: 3 outline stars thrown upwards from the star (speed_scale 3: 450 px/s, 882 px/s^2, 1.33 s), growing 0.5 -> 1.0, fading */
static void star_particles(float cx, float cy, float since_ms, int seed) {
    const float t = since_ms / 1000.0f;
    if (t < 0.0f || t > 1.33f || bb_ui_reduce_motion()) return;
    const float K = 240.0f / 1080.0f;
    for (int p = 0; p < 3; p++) {
        const uint32_t h = (uint32_t)(seed * 7919 + p * 104729) * 2654435761u;
        const float spread = ((float)((h >> 8) & 1023) / 1023.0f - 0.5f) * 1.5708f;   /* 45 degrees either side of up */
        const float spd = 450.0f * K * (0.5f + 0.5f * (float)((h >> 18) & 255) / 255.0f);
        const float ox = ((float)((h >> 3) & 31) / 31.0f - 0.5f) * 12.0f;
        const float x = cx + ox + sinf(spread) * spd * t, y = cy - cosf(spread) * spd * t + 0.5f * 882.0f * K * t * t;
        const float life = t / 1.33f, sz = 16.0f * (0.5f + 0.5f * life) * 1.4f;
        if (bb_theme_halloween()) bb_art_hw_cell(2, x, y, sz, (float)(p - 1) * 1.2f * t, rgba(0xFF, 0xFF, 0xFF, 1.0f - life), ZO + 0.12f);   /* GradeStar: candy_corn_filled particles */
        else bb_art_star_rot(true, x, y, sz, (float)(p - 1) * 1.2f * t, rgba(0xFF, 0xFF, 0xFF, 1.0f - life), ZO + 0.12f);
    }
}

static void stat_bar(float x, float y, float w, float h, const char* name, const char* value, float a) {
    const u32 border = rgba(0xCC, 0xCC, 0xCC, a);
    rrect(x, y, w, h, 3.0f, border, ZO + 0.05f);
    rrect(x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, 2.0f, rgba(0x00, 0x00, 0x00, a), ZO + 0.06f);
    T(BB_FACE_BOLD, 14, x + 8.0f, y + (h - 19.0f) * 0.5f, white(a), C2D_AlignLeft, name);
    T(BB_FACE_BOLD, 14, x + w - 8.0f, y + (h - 19.0f) * 0.5f, white(a), C2D_AlignRight, value);
}

static const float k_btn_cx[2] = { 121.0f, 199.0f }, k_btn_y = 204.0f, k_btn_w = 74.0f, k_btn_h = 22.0f;

int bb_results_hit(int px, int py, int64_t ms) {
    if (ms < BB_RESULTS_ANIM_MS) return -1;
    for (int i = 0; i < 2; i++) {
        if ((float)px >= k_btn_cx[i] - k_btn_w * 0.5f - 4.0f && (float)px < k_btn_cx[i] + k_btn_w * 0.5f + 4.0f &&
            (float)py >= k_btn_y - 4.0f && (float)py < k_btn_y + k_btn_h + 4.0f) return i;
    }
    return -1;
}

void bb_results_draw(const bb_results_t* r, int64_t ms, int focus) {
    const float e = (float)ms;
    char buf[48];
    const float in = bb_ui_ease_out(e / 1000.0f);   /* show_stars 0 -> 1 s, transition 0.2 (ease out) */
    const bool done = e >= 4000.0f;

    /* BlackBacking 0 -> 0.957 over the frozen highway */
    C2D_DrawRectSolid(0, 0, ZO, 320, 240, rgba(0, 0, 0, 0.957f * in));

    /* Score: "SCORE" grey + amount orange, both with a drop shadow; the root hovers 5 px over 4 s */
    const float hover = -5.0f * (240.0f / 1080.0f) * 0.5f * (1.0f - cosf(bb_ui_time() * 3.14159f / 2.0f));
    const float sy = rs_y(200.0f + (173.0f - 200.0f) * in) - 13.0f + hover;
    const float cp = e < 2000.0f ? 0.0f : (e >= 4000.0f ? 1.0f : (e - 2000.0f) / 2000.0f);
    snprintf(buf, sizeof(buf), "%06ld", (long)((float)r->score * cp));
    T(BB_FACE_BOLD, 26, rs_x(941.0f) + 1.0f, sy + 1.0f, rgba(0x3A, 0x3A, 0x3A, in), C2D_AlignRight, "SCORE");
    T(BB_FACE_BOLD, 26, rs_x(941.0f), sy, rgba(0xB9, 0xB9, 0xB9, in), C2D_AlignRight, "SCORE");
    T(BB_FACE_BOLD, 26, rs_x(964.0f) + 1.0f, sy + 1.0f, rgba(0x80, 0x4E, 0x07, in), C2D_AlignLeft, buf);
    T(BB_FACE_BOLD, 26, rs_x(964.0f), sy, rgba(0xED, 0x9F, 0x12, in), C2D_AlignLeft, buf);

    /* Personal best (modulate 1.96 brightens the grey label) */
    const float py = rs_y(280.0f + (255.0f - 280.0f) * in) - 4.0f;
    snprintf(buf, sizeof(buf), "%06ld", (long)r->pb);
    const float lw = bb_font_width(BB_FACE_BOLD, 12, "Personal Best "), vw = bb_font_width(BB_FACE_BOLD, 12, buf);
    T(BB_FACE_BOLD, 12, RS_CX - (lw + vw) * 0.5f, py, rgba(0xDA, 0xDA, 0xDA, in), C2D_AlignLeft, "Personal Best ");
    T(BB_FACE_BOLD, 12, RS_CX - (lw + vw) * 0.5f + lw, py, white(in), C2D_AlignLeft, buf);

    /* Line2D: grey 600 px line with a lighter 200 px centre, scaling out from the middle */
    const float ly = rs_y(310.0f) + 4.0f;   /* kept clear of the personal-best line at this text size */
    C2D_DrawRectSolid(RS_CX - 66.0f * in, ly, ZO + 0.03f, 132.0f * in, 1.0f, rgba(0x49, 0x49, 0x49, 1.0f));
    C2D_DrawRectSolid(RS_CX - 22.0f * in, ly, ZO + 0.04f, 44.0f * in, 1.0f, rgba(0x86, 0x86, 0x86, 1.0f));

    /* Stars: 5 outlines (6 and 7 appear only when reached), filled one by one; root pushed back 90 px for each star past the fifth */
    const float star_y = rs_y(506.0f + (430.0f - 506.0f) * in);
    float push = 0.0f;
    for (int i = 5; i < r->grade_n && i < 7; i++) push -= 90.0f * bb_ui_ease_out((e - star_ms(i, r->grade_n)) / 300.0f) * (e >= star_ms(i, r->grade_n) ? 1.0f : 0.0f);
    const bool broken = done && r->accuracy < 70.0f;
    float shake_x = 0.0f, shake_y = 0.0f, star_scale = 1.0f, star_dim = 1.0f, star_a = in;
    if (broken) {   /* "break": a 0.2 s shake and the stars dim to 0.605 grey at 0.708 alpha */
        static const float kx[6] = { 11, -2, 6, 3, 1, 0 }, ky[6] = { -12, 0, 5, 0, -3, 0 };
        const int k = (int)((e - 4000.0f) / 40.0f);
        if (k < 6 && !bb_ui_reduce_motion()) { shake_x = kx[k] * (240.0f / 1080.0f); shake_y = ky[k] * (240.0f / 1080.0f); }
        const float f = bb_ui_ease_out((e - 4000.0f) / 250.0f);
        star_dim = 1.0f - 0.395f * f;
        star_a = 1.0f - 0.292f * f;
    } else if (done && !bb_ui_reduce_motion()) {   /* "hover": scale 1 -> 1.025 -> 1 over 4 s */
        star_scale = 1.0f + 0.0125f * (1.0f - cosf((e - 4000.0f) / 1000.0f * 3.14159f / 2.0f));
    }
    const float base = 100.0f * 1.4f * (240.0f / 1080.0f);   /* GradeStar: 100 px texture at scale 1.4 */
    float sx[7], ssz[7];
    bool sfill[7], sshow[7];
    for (int i = 0; i < 7; i++) {
        const float t_fill = star_ms(i, r->grade_n);
        sfill[i] = i < r->grade_n && e >= t_fill;
        sshow[i] = i < 5 || sfill[i];
        sx[i] = RS_CX + ((float)(i - 2) * 180.0f + push) * star_scale * (240.0f / 1080.0f) + shake_x;
        ssz[i] = base * star_scale;
        if (sfill[i]) {   /* show_star: scale 1.7 -> 1.5 (cubic out, 0.6 s) */
            const float f = (e - t_fill) / 600.0f;
            const float k = f >= 1.0f ? 1.0f : 1.0f - (1.0f - f) * (1.0f - f) * (1.0f - f);
            ssz[i] = base / 1.4f * (1.7f - 0.2f * k) * star_scale;
        }
    }
    const float y = star_y + shake_y;
    for (int i = 0; i < 7; i++) {   /* ripples first (note_beatbar_circle_pulse: a gold ring opening behind the star) */
        if (!sfill[i]) continue;
        const float f = (e - star_ms(i, r->grade_n)) / 600.0f;
        if (f >= 1.0f) continue;
        const float k = 1.0f - (1.0f - f) * (1.0f - f) * (1.0f - f);
        const float rr = ssz[i] * (0.45f + 0.55f * k), w = 2.5f * (1.0f - k) + 0.8f;
        for (int seg = 0; seg < 24; seg++) {
            const float a0 = (float)seg * 0.261799f, a1 = a0 + 0.261799f;
            const u32 c = bb_theme_halloween() ? rgba(0xFF, 0x7A, 0x2A, 0.8f * (1.0f - k)) : rgba(0xE8, 0xB1, 0x48, 0.8f * (1.0f - k));   /* GradeStar: ripple #ff7a2a in Halloween */
            C2D_DrawTriangle(sx[i] + cosf(a0) * rr, y + sinf(a0) * rr, c, sx[i] + cosf(a1) * rr, y + sinf(a1) * rr, c, sx[i] + cosf(a0) * (rr - w), y + sinf(a0) * (rr - w), c, ZO + 0.08f);
            C2D_DrawTriangle(sx[i] + cosf(a1) * rr, y + sinf(a1) * rr, c, sx[i] + cosf(a1) * (rr - w), y + sinf(a1) * (rr - w), c, sx[i] + cosf(a0) * (rr - w), y + sinf(a0) * (rr - w), c, ZO + 0.08f);
        }
    }
    const u8 v = (u8)(255.0f * star_dim);
    for (int i = 0; i < 7; i++) {
        if (!sshow[i]) continue;
        if (bb_theme_halloween()) bb_art_hw_cell(sfill[i] ? 2 : 1, sx[i], y, ssz[i] * 1.05f, 0.0f, C2D_Color32(v, v, v, (u8)(255.0f * star_a)), ZO + 0.10f);   /* GradeStar 'halloween' */
        else bb_art_star(!sfill[i], sx[i], y, ssz[i], C2D_Color32(v, v, v, (u8)(255.0f * star_a)), ZO + 0.10f);
    }
    for (int i = 0; i < 7; i++) if (sfill[i]) star_particles(sx[i], y, e - star_ms(i, r->grade_n), i);

    /* Stat bars: Notes Hit, Max Combo (x), Accuracy (%), counting up over 2 s from 2 s; faded in 0.3 -> 1.3 s */
    const float sa = (e - 300.0f) / 1000.0f < 0.0f ? 0.0f : ((e - 300.0f) / 1000.0f > 1.0f ? 1.0f : (e - 300.0f) / 1000.0f);
    char v1[24], v2[24], v3[24];
    snprintf(v1, sizeof(v1), "%lu", (unsigned long)((float)r->hit * cp));
    snprintf(v2, sizeof(v2), "%lux", (unsigned long)((float)r->max_combo * cp));
    snprintf(v3, sizeof(v3), "%d%%", (int)(r->accuracy * cp));
    stat_bar(60.0f, 128.0f, 200.0f, 19.0f, "Notes Hit", v1, sa);
    stat_bar(60.0f, 150.0f, 200.0f, 19.0f, "Max Combo", v2, sa);
    stat_bar(60.0f, 172.0f, 200.0f, 19.0f, "Accuracy", v3, sa);

    /* Continue / Retry (visible from 4 s, Continue focused; UiButton: focused white, others dim grey) */
    if (done) {
        static const char* const lab[2] = { "Continue", "Retry" };
        for (int i = 0; i < 2; i++) {
            const bool f = i == focus;
            T(BB_FACE_BOLD, 17, k_btn_cx[i], k_btn_y, f ? white(1.0f) : rgba(0x69, 0x69, 0x69, 1.0f), C2D_AlignCenter, lab[i]);
        }
    }

    /* WhiteFlash: 1 -> 0 from 4 s to 5 s (depths stay below 1.0: ZO is 0.70) */
    if (e >= 4000.0f && e < 5000.0f) {
        const float f = (e - 4000.0f) / 1000.0f;
        C2D_DrawRectSolid(0, 0, ZO + 0.25f, 320, 240, rgba(255, 255, 255, (bb_ui_reduce_motion() ? 0.4f : 1.0f) * (1.0f - sqrtf(f))));
    }

    /* reward banners: one at a time, dropping in from the top edge and rising away again */
    toast_t tl[6];
    const int tn = toast_list(r, tl);
    const int idx = e >= (float)TOAST_T0 ? (int)((e - (float)TOAST_T0) / (float)TOAST_EACH) : -1;
    if (idx >= 0 && idx < tn) {
        const float local = e - (float)TOAST_T0 - (float)TOAST_EACH * (float)idx;
        if (local < (float)TOAST_SHOW) {
            const float k_in = bb_ui_ease_out(local / 250.0f);
            const float k_out = local > TOAST_SHOW - 250.0f ? bb_ui_ease_out((local - (TOAST_SHOW - 250.0f)) / 250.0f) : 0.0f;
            const float w = 208.0f, h = 32.0f, x = 160.0f - w * 0.5f;
            const float y = -h - 4.0f + (h + 10.0f) * k_in - (h + 10.0f) * k_out;
            const float a = 1.0f - k_out;
            const bool gold = tl[idx].kind == 0;
            rrect(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, 6.0f, gold ? rgba(0xE8, 0xB1, 0x48, a) : rgba(0xCC, 0xCC, 0xCC, a), ZO + 0.26f);
            rrect(x, y, w, h, 5.0f, rgba(0x0C, 0x0C, 0x10, 0.96f * a), ZO + 0.27f);
            bb_art_icon(6, x + 17.0f, y + h * 0.5f, 20.0f, C2D_Color32(0xFF, 0xFF, 0xFF, (u8)(255.0f * a)), ZO + 0.28f);
            bb_font_draw(BB_FACE_BOLD, 10, x + 33.0f, y + 3.0f, gold ? rgba(0xE8, 0xB1, 0x48, a) : rgba(0xED, 0x9F, 0x12, a), C2D_AlignLeft, tl[idx].line1, ZO + 0.29f);
            snprintf(buf, sizeof(buf), "%.40s", tl[idx].line2);
            bb_font_draw(BB_FACE_BOLD, 12, x + 33.0f, y + 15.0f, white(a), C2D_AlignLeft, buf, ZO + 0.29f);
        }
    }
}

/* Top screen: the stage (the climax video frozen on its last frame) dims with the results, as the BlackBacking rises below. */
void bb_results_draw_top(const bb_results_t* r, int64_t ms) {
    (void)r;
    C2D_DrawRectSolid(0, 0, ZO, 400, 240, rgba(0, 0, 0, 0.62f * bb_ui_ease_out((float)ms / 1000.0f)));
}

/* ---------------------------------------------------------------- pause (PauseMenu.tscn + game_pause_menu.gd) */
/* "pause" animation (0.4 s): ColorRect to 0.778 black, the black Backing (sawtooth edge) slides in from the left, the "Paused" label
 * (160 px, [wave freq=7 amp=50]) and the level line fade in. Buttons: Resume, Restart, Settings, Level Select, Exit To Menu. */
static const char* const k_pause_items[BB_PAUSE_ITEMS] = { "Resume", "Restart", "Settings", "Level Select", "Exit To Menu" };

void bb_flow_pause_top(int64_t t_ms, float alpha) {
    const float e = bb_ui_ease_out((float)t_ms / 400.0f) * alpha;
    C2D_DrawRectSolid(0, 0, ZO, 400, 240, rgba(0, 0, 0, 0.778f * e));
    /* "Paused", each letter riding its own sine (RichTextLabel wave, connected=no) */
    static const char* const word = "Paused";
    const int size = 56;
    float w = bb_font_width(BB_FACE_HEAD, size, word);
    float x = 200.0f - w * 0.5f;
    for (int i = 0; word[i]; i++) {
        char ch[2] = { word[i], 0 };
        const float dy = bb_ui_reduce_motion() ? 0.0f : sinf(bb_ui_time() * 7.0f + (float)i * 0.9f) * 3.0f;
        bb_font_draw(BB_FACE_HEAD, size, x + 2.0f, 74.0f + dy + 3.0f, rgba(0x40, 0x40, 0x48, 0.8f * e), C2D_AlignLeft, ch, ZO + 0.09f);
        bb_font_draw(BB_FACE_HEAD, size, x, 74.0f + dy, white(e), C2D_AlignLeft, ch, ZO + 0.10f);
        x += bb_font_width(BB_FACE_HEAD, size, ch);
    }
}

static float pause_item_y(int i) { return 50.0f + (float)i * 34.0f; }

void bb_flow_pause_bottom(int cursor, const char* level_line, int64_t t_ms, float alpha) {
    const float e = bb_ui_ease_out((float)t_ms / 400.0f);
    C2D_DrawRectSolid(0, 0, ZO, 320, 240, rgba(0, 0, 0, 0.778f * e * alpha));
    /* Backing: a black panel with a sawtooth right edge, sliding in */
    const float px = -230.0f * (1.0f - e), edge = 214.0f + px;
    const u32 bk = rgba(0, 0, 0, alpha);
    C2D_DrawRectSolid(0, 0, ZO + 0.01f, edge > 0.0f ? edge : 0.0f, 240, bk);
    for (int k = 0; k < 20; k++) {
        const float y = (float)k * 12.0f;
        C2D_DrawTriangle(edge, y, bk, edge + 9.0f, y + 6.0f, bk, edge, y + 12.0f, bk, ZO + 0.01f);
    }
    const float la = e * alpha;
    T(BB_FACE_BOLD, 14, 14.0f + px, 10.0f, rgba(0xB8, 0xB8, 0xC0, la), C2D_AlignLeft, level_line);
    C2D_DrawRectSolid(10.0f + px, 33.0f, ZO + 0.02f, 186.0f, 1.0f, rgba(0xFF, 0xFF, 0xFF, 0.222f * la));   /* Line2D */
    static float hy = -1.0f;
    const float ty = pause_item_y(cursor);
    if (hy < 0.0f || bb_ui_enter_time() < 0.03f) hy = ty;
    bb_ui_smooth(&hy, ty, 22.0f);
    /* focused UiButton: a white band fading out to the right behind the text */
    C2D_DrawRectangle(px, hy - 2.0f, ZO + 0.02f, 196.0f, 28.0f, rgba(0xFF, 0xFF, 0xFF, 0.22f * la), rgba(0xFF, 0xFF, 0xFF, 0.0f), rgba(0xFF, 0xFF, 0xFF, 0.22f * la), rgba(0xFF, 0xFF, 0xFF, 0.0f));
    for (int i = 0; i < BB_PAUSE_ITEMS; i++) {
        const bool sel = i == cursor;
        const u32 c = sel ? white(la) : rgba(0x9A, 0x9A, 0xA4, la);
        bb_font_draw_smallcaps(BB_FACE_BOLD, 21, 16.0f + px, pause_item_y(i), c, C2D_AlignLeft, k_pause_items[i], ZO + 0.10f);
        bb_ui_zone_add(BB_ZONE_PAUSE + i, 0, pause_item_y(i) - 4.0f, 214.0f, 32.0f);
    }
}

/* ---------------------------------------------------------------- resume countdown (PauseMenu "countdown", 3 s) */
/* CountDownFade: 0 -> 0.81 black in 0.1 s, back to 0 at 3 s; "3", "2", "1" one per second, each fading in over 0.5 s and out over the
 * next 0.5 s; the menu fades out over 0.47 s (drawn by the caller with bb_flow_countin_menu_alpha). The song stays paused throughout. */
void bb_flow_countin(float t_s) {
    if (t_s >= 3.0f) return;
    const float fade = t_s < 0.1f ? 0.81f * t_s / 0.1f : 0.81f * (1.0f - (t_s - 0.1f) / 2.9f);
    C2D_DrawRectSolid(0, 0, ZO, 400, 240, rgba(0, 0, 0, fade));
    static const char* const nums[3] = { "3", "2", "1" };
    const int k = (int)t_s;
    const float f = t_s - (float)k;
    const float a = f < 0.5f ? f / 0.5f : 1.0f - (f - 0.5f) / 0.5f;
    bb_font_draw_xf(BB_FACE_HEAD, 56, 202.0f, 122.0f, 1.6f, 0.0f, rgba(0, 0, 0, 0.6f * a), nums[k], ZO + 0.08f);
    bb_font_draw_xf(BB_FACE_HEAD, 56, 200.0f, 120.0f, 1.6f, 0.0f, white(a), nums[k], ZO + 0.10f);
}

float bb_flow_countin_menu_alpha(float t_s) { return t_s >= 0.4667f ? 0.0f : 1.0f - t_s / 0.4667f; }

/* ---------------------------------------------------------------- level failed (GameOver.tscn "gameover", 4 s) */
/* The two screens are treated as one tall canvas: the top screen is y 0..240, the bottom screen y 280..520 (40 px hinge).
 * 0 s: glass break + studio hiss, red shockwave and "Level Failed" (shake rate 10 / level 15) small in the middle of the BOTTOM screen,
 * jolting through the original's six shake keys in 0.25 s, with the red ripple bursting out under it; it eases to rest by 1.3 s,
 * then rises quickly (0.6 -> 1.3 s; the original climbs 540 -> 150 from 1.3 to 3 s) and grows into the TOP screen, where it keeps shaking over a
 * translucent bar. The top screen is wiped fully black by the diagonal slab (Polygon2D, scale y 0 -> 1.6 in 1 s). The bottom screen
 * fades to the stage's game-over art (1.6-3 s) with the caption (2-3 s) and Retry / Give Up (1.71-2.95 s, Retry focused). */
#define FAIL_HINGE 40.0f
#define FAIL_RISE_T0 0.6f   /* the text leaves the bottom screen ... */
#define FAIL_RISE_T1 1.3f   /* ... and has settled on the top screen */
static const char* const k_fail_captions[3] = { "BETTER LUCK NEXT TIME...", "LOOKS LIKE YOU COULDN'T KEEP UP.", "BACK TO THE DRAWING BOARD." };

static float fail_ease(float t) { return bb_ui_ease_out(t); }

/* text centre on the tall canvas and its scale (1 = Anton 56) at time e (s) */
static void fail_anchor(float e, float* vy, float* vx, float* sc) {
    static const float kt[6] = { 0.0f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f };
    static const float kx[6] = { 890, 1019, 926, 985, 958, 962.1f }, ky[6] = { 579, 515, 525, 533, 555, 549 };
    const float K = 240.0f / 1080.0f;
    const float base_y = 240.0f + FAIL_HINGE + 120.0f;   /* middle of the bottom screen */
    float x = 960.0f, y = 540.0f;
    if (e < 0.25f) {
        int i = 0;
        while (i < 4 && e >= kt[i + 1]) i++;
        const float f = (e - kt[i]) / (kt[i + 1] - kt[i]);
        x = kx[i] + (kx[i + 1] - kx[i]) * f;
        y = ky[i] + (ky[i + 1] - ky[i]) * f;
    } else if (e < FAIL_RISE_T0) {
        const float f = fail_ease((e - 0.25f) / (FAIL_RISE_T0 - 0.25f));
        x = 962.1f + (960.0f - 962.1f) * f;
        y = 549.0f + (540.0f - 549.0f) * f;
    }
    if (bb_ui_reduce_motion() && e < FAIL_RISE_T0) { x = 960.0f; y = 540.0f; }
    *vx = 200.0f + (x - 960.0f) * K;
    const float rest_y = base_y + (y - 540.0f) * K;
    /* faster than the original's linear 1.3 -> 3 s climb (user direction): an eased lift between FAIL_RISE_T0 and FAIL_RISE_T1 */
    float rise = e < FAIL_RISE_T0 ? 0.0f : (e >= FAIL_RISE_T1 ? 1.0f : (e - FAIL_RISE_T0) / (FAIL_RISE_T1 - FAIL_RISE_T0));
    rise = rise < 0.5f ? 4.0f * rise * rise * rise : 1.0f - powf(-2.0f * rise + 2.0f, 3.0f) * 0.5f;
    const float end_y = 96.0f;                       /* settled on the top screen */
    *vy = rest_y + (end_y - rest_y) * rise;
    *sc = 0.5f + 0.5f * rise;                         /* small on the bottom screen, full size on the top */
}

/* "Level Failed" with per-letter shake, drawn at (cx, cy) in screen coordinates */
static void fail_text(float cx, float cy, float sc, float a, float e, float z) {
    static const char* const word = "Level Failed";
    const float w = bb_font_width(BB_FACE_HEAD, 56, word) * sc;
    float x = cx - w * 0.5f;
    const int tick = (int)(e * 10.0f);                /* rate = 10 per second */
    for (int i = 0; word[i]; i++) {
        char ch[2] = { word[i], 0 };
        const float cw = bb_font_width(BB_FACE_HEAD, 56, ch) * sc;
        float dx = 0.0f, dy = 0.0f;
        if (!bb_ui_reduce_motion()) {
            const uint32_t h = (uint32_t)(tick * 7919 + i * 104729) * 2654435761u;
            dx = (((float)((h >> 8) & 255) / 255.0f) - 0.5f) * 3.0f * sc;
            dy = (((float)((h >> 16) & 255) / 255.0f) - 0.5f) * 3.0f * sc;
        }
        bb_font_draw_xf(BB_FACE_HEAD, 56, x + cw * 0.5f + dx + 2.0f * sc, cy + dy + 3.0f * sc, sc, 0.0f, rgba(0, 0, 0, 0.7f * a), ch, z);
        bb_font_draw_xf(BB_FACE_HEAD, 56, x + cw * 0.5f + dx, cy + dy, sc, 0.0f, rgba(0xE6, 0x17, 0x2B, a), ch, z + 0.01f);
        x += cw;
    }
}

void bb_flow_fail_top(int64_t t_ms, uint32_t caption) {
    const float e = (float)t_ms / 1000.0f;
    /* the black slab (Polygon2D rotated 18 degrees, scale y 0 -> 1.6 in 1 s) wipes across until the top screen is fully black */
    {
        const float f = fail_ease(e / 1.0f);
        const float ang = 0.31416f, ca = cosf(ang), sa = sinf(ang);
        const float h = 520.0f * f, cx = 200.0f, cy = 120.0f, hw = 400.0f;   /* 520 px band covers the 400x240 screen at any angle */
        const u32 k = rgba(0, 0, 0, 1.0f);
        const float nx = -sa * h * 0.5f, ny = ca * h * 0.5f;
        if (h > 0.5f) {
            C2D_DrawTriangle(cx - ca * hw - nx, cy - sa * hw - ny, k, cx + ca * hw - nx, cy + sa * hw - ny, k, cx + ca * hw + nx, cy + sa * hw + ny, k, ZO + 0.01f);
            C2D_DrawTriangle(cx - ca * hw - nx, cy - sa * hw - ny, k, cx + ca * hw + nx, cy + sa * hw + ny, k, cx - ca * hw + nx, cy - sa * hw + ny, k, ZO + 0.01f);
        }
        if (f >= 0.999f) C2D_DrawRectSolid(0, 0, ZO + 0.011f, 400, 240, k);
    }
    float vx, vy, sc;
    fail_anchor(e, &vy, &vx, &sc);
    if (vy < 240.0f + 40.0f) {   /* on (or entering) the top screen */
        const float pa = e < FAIL_RISE_T1 ? 0.0f : fail_ease((e - FAIL_RISE_T1) / 0.5f);
        if (pa > 0.0f) {         /* the translucent bar behind the text (gradient panel, 0.24) */
            const u32 c0 = rgba(0xFF, 0xFF, 0xFF, 0.0f), c1 = rgba(0xFF, 0xFF, 0xFF, 0.24f * pa);
            C2D_DrawRectangle(10.0f, vy - 14.0f, ZO + 0.02f, 190.0f, 34.0f, c0, c1, c0, c1);
            C2D_DrawRectangle(200.0f, vy - 14.0f, ZO + 0.02f, 190.0f, 34.0f, c1, c0, c1, c0);
        }
        fail_text(vx, vy, sc, 1.0f, e, ZO + 0.10f);
    }
    (void)caption;
}

static const float k_fail_btn_x[2] = { 108.0f, 212.0f };

void bb_flow_fail_bottom(int64_t t_ms, int focus, uint32_t caption) {
    const float e = (float)t_ms / 1000.0f;
    C2D_DrawRectSolid(0, 0, ZO, 320, 240, rgba(0, 0, 0, 0.6f * fail_ease(e / 0.4f)));
    /* the stage's game-over art, centre of the 400x240 picture */
    const float ga = e < 1.6f ? 0.0f : fail_ease((e - 1.6f) / 1.4f);
    if (ga > 0.0f) {
        bb_view_draw_gameover_art(0, 0, 320, 240, 40.0f, ga, ZO + 0.02f);
        C2D_DrawRectangle(0, 150.0f, ZO + 0.03f, 320, 90.0f, rgba(0, 0, 0, 0.0f), rgba(0, 0, 0, 0.0f), rgba(0, 0, 0, 0.8f * ga), rgba(0, 0, 0, 0.8f * ga));
    }
    /* red ripple (Shockwave: note_beatbar_circle_pulse frames 0 -> 20 in 0.47 s, scale 10.3, modulate 0.6 / 0.086 / 0.086): a thick
     * donut bursting out from the middle, thinning into a ring that runs off the screen and fades */
    if (e < 0.47f) {
        static const float ro[21] = { 0.00f, 0.16f, 0.22f, 0.28f, 0.32f, 0.35f, 0.37f, 0.38f, 0.39f, 0.40f, 0.40f, 0.41f, 0.42f, 0.43f, 0.43f, 0.44f, 0.44f, 0.45f, 0.45f, 0.45f, 0.45f };
        static const float ri[21] = { 0.00f, 0.06f, 0.06f, 0.07f, 0.07f, 0.20f, 0.27f, 0.30f, 0.33f, 0.35f, 0.37f, 0.38f, 0.40f, 0.41f, 0.42f, 0.43f, 0.43f, 0.44f, 0.44f, 0.445f, 0.45f };
        const float fr = e / 0.47f * 20.0f;
        const int i = (int)fr;
        const float t = fr - (float)i;
        const int j = i < 20 ? i + 1 : 20;
        const float cell = 256.0f * 10.3f * (240.0f / 1080.0f);   /* the frame cell on this screen (~586 px) */
        const float R = (ro[i] + (ro[j] - ro[i]) * t) * cell, r = (ri[i] + (ri[j] - ri[i]) * t) * cell;
        const float fade = fr < 15.0f ? 1.0f : 1.0f - (fr - 15.0f) / 5.0f;
        const u32 c = rgba(0x99, 0x16, 0x16, fade);
        if (R > 1.0f && !bb_ui_reduce_motion()) {
            for (int sgm = 0; sgm < 40; sgm++) {
                const float a0 = (float)sgm * 0.15708f, a1 = a0 + 0.15708f;
                const float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
                C2D_DrawTriangle(160.0f + c0 * R, 120.0f + s0 * R, c, 160.0f + c1 * R, 120.0f + s1 * R, c, 160.0f + c0 * r, 120.0f + s0 * r, c, ZO + 0.04f);
                C2D_DrawTriangle(160.0f + c1 * R, 120.0f + s1 * R, c, 160.0f + c1 * r, 120.0f + s1 * r, c, 160.0f + c0 * r, 120.0f + s0 * r, c, ZO + 0.04f);
            }
        }
    }
    float vx, vy, sc;
    fail_anchor(e, &vy, &vx, &sc);
    const float by = vy - (240.0f + FAIL_HINGE);
    if (by > -40.0f) fail_text(vx - 40.0f, by, sc, 1.0f, e, ZO + 0.10f);   /* (the bottom screen is 80 px narrower: same centre) */
    const float ca2 = e < 2.0f ? 0.0f : fail_ease((e - 2.0f) / 1.0f);   /* CaptionLabel, 2 -> 3 s */
    if (ca2 > 0.0f) T(BB_FACE_BOLD, 14, 160.0f, 36.0f, rgba(0xE8, 0xE8, 0xF0, ca2), C2D_AlignCenter, k_fail_captions[caption % 3]);   /* just above the art's figures */
    /* Retry / Give Up (generic text buttons: focused white, the other dim grey) */
    const float ba = e < 1.71f ? 0.0f : bb_ui_ease_out((e - 1.71f) / 1.24f);
    if (ba > 0.0f) {
        static const char* const lab[2] = { "Retry", "Give Up" };
        for (int i = 0; i < 2; i++) {
            const bool f = i == focus;
            bb_font_draw_smallcaps(BB_FACE_BOLD, f ? 26 : 21, k_fail_btn_x[i], f ? 196.0f : 199.0f, f ? white(ba) : rgba(0x8A, 0x8A, 0x8A, ba), C2D_AlignCenter, lab[i], ZO + 0.10f);
            bb_ui_zone_add(BB_ZONE_FAIL + i, k_fail_btn_x[i] - 52.0f, 186.0f, 104.0f, 44.0f);
        }
    }
}

