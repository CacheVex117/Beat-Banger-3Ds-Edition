/**
 * @file game_view.c
 * @brief Top-screen gameplay presentation (see game_view.h).
 *
 * Geometry is the reference's 1920x1080 canvas mapped to 400x240 with "cover" scaling
 * (K = 240/1080); the horizontal overflow (13 px per side) is cropped.
 *   character: world = (960,540) + 1.5 * (cam_pos + cam_scale * local), frame = 1280x720 local
 *   lanes:     x = 750/890/1030/1170, hit line y = 984, scroll 350 canvas px/s (speed mod 1.0)
 */
#include "game_view.h"
#include "theme.h"
#include "ui_art.h"
#include "font.h"
#include "ui_motion.h"
#include "flow_ui.h"
#include "ui_sfx.h"
#include "input_map.h"
#include "video.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define K            (240.0f / 1080.0f)
#define CANVAS_CX    960.0f
#define CANVAS_CY    540.0f
#define CHAR_SCALE   1.5f

#define Z_BG 0.10f
#define Z_CHAR 0.20f
#define Z_LANE 0.30f
#define Z_NOTE 0.40f
#define Z_HUD 0.50f
#define Z_FX 0.60f
#define Z_OVER 0.70f

/* DOWNSCROLL_LANE_ORDER maps input_type -> lane slot; slot -> canvas x */
static const int   k_lane_slot[4] = { 2, 3, 0, 1 };
/* UI.default_theme colors for game_note_0..3 */

static u32 lane_color(uint8_t t, uint8_t alpha) {
    if (bb_theme_halloween()) {   /* UI.halloween_theme: MEDIUM_PURPLE, DEEP_PINK, LIME_GREEN, LIGHT_BLUE */
        switch (t & 3) {
            case 0:  return C2D_Color32(0x93, 0x70, 0xDB, alpha);
            case 1:  return C2D_Color32(0xFF, 0x14, 0x93, alpha);
            case 2:  return C2D_Color32(0x32, 0xCD, 0x32, alpha);
            default: return C2D_Color32(0xAD, 0xD8, 0xE6, alpha);
        }
    }
    switch (t & 3) {
        case 0:  return C2D_Color32(0x1A, 0x8C, 0xC7, alpha); /* (0.10,0.55,0.78) */
        case 1:  return C2D_Color32(0xED, 0x78, 0x0D, alpha); /* (0.93,0.47,0.05) */
        case 2:  return C2D_Color32(0xC7, 0x2B, 0x17, alpha); /* (0.78,0.17,0.09) */
        default: return C2D_Color32(0xAB, 0x2B, 0x63, alpha); /* (0.67,0.17,0.39) */
    }
}

u32 bb_view_lane_color(int input_type, u8 alpha) { return lane_color((uint8_t)input_type, alpha); }
int bb_view_lane_at_slot(int slot) {
    for (int t = 0; t < 4; t++) if (k_lane_slot[t] == (slot & 3)) return t;
    return 0;
}

/* Bottom-screen press feedback (presentation only, the judgment code is untouched): a held lane glows above its receptor and
 * every judged hit rings out from the receptor in the judgment colour. Times are chart time, so they freeze with the pause. */
static uint8_t g_held_lanes;
static float g_press_t[4] = { -10.0f, -10.0f, -10.0f, -10.0f };   /* UI clock of each lane's last physical press (receptor bounce) */
void bb_view_set_held_lanes(uint8_t mask) { g_held_lanes = mask; }
void bb_view_lanes_pressed(uint8_t mask) {
    for (int l = 0; l < 4; l++) if (mask & (1u << l)) g_press_t[l] = bb_ui_time();
}
/* NoteDetector AccuracyText (judgement + combo + points), ScoreDisplay pulse, GameStars meter */
static int   g_judge_points;          /* "+100" / "+0" / "-20" */
static const char* g_judge_word;
static bool  g_judge_red;
static float g_judge_t = -10.0f, g_score_pulse_t = -10.0f, g_star_pulse_t = -10.0f, g_star_refill_t[5] = { -10, -10, -10, -10, -10 };
static int   g_stars_active = 5;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* Book layout (Legacy mods, LEGACY LAYOUT modifier): the 3DS is held turned a quarter to the left, touch screen on the right.
 * The side highway is used as is (in the hand the notes fall from the top onto receptors at the bottom); its HUD band grows to a
 * 72 px column that is drawn upright for that hold, and the top screen shows the portrait art upright and as large as fits. */
static bool  g_book = false;
static float g_book_u0 = 0.2656f, g_book_u1 = 0.7344f;   /* the art's horizontal span inside a 1280-wide frame cell */
void bb_view_set_book(bool on, float u0, float u1) {
    g_book = on;
    if (u1 - u0 > 0.05f && u0 >= 0.0f && u1 <= 1.0f) { g_book_u0 = u0; g_book_u1 = u1; }
}
static float expo_out(float t) { return t >= 1.0f ? 1.0f : 1.0f - powf(2.0f, -10.0f * t); }

static float g_text_z = Z_HUD; /* overlays raise this above their dim rectangle */

static bool g_system_text = false;   /* bottom screen: its typography is the user's to design, keep the system font */
void bb_view_set_system_text(bool on) { g_system_text = on; }

static void text(C2D_TextBuf tb, float x, float y, float scale, u32 color, u32 align, const char* s) {
    if (bb_font_ready() && !g_system_text) {   /* original UI font (Encode Sans SC Condensed / Anton), see font.c */
        bb_face_t face;
        int size;
        bb_font_from_scale(scale, &face, &size);
        bb_font_draw(face, size, x, y, color, align, s, g_text_z + 0.01f);
        return;
    }
    C2D_Text t;
    C2D_TextParse(&t, tb, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | align, x + 1.0f, y + 1.0f, g_text_z + 0.01f, scale, scale, C2D_Color32(0, 0, 0, 0xC0));
    C2D_DrawText(&t, C2D_WithColor | align, x, y, g_text_z + 0.02f, scale, scale, color);
}

/* ---- shared UI textures (tools/convert_ui.py): ui.t3x atlas 64x64, checker.t3x 32x32 repeating ---- */
static C2D_SpriteSheet g_ui_sheet, g_checker_sheet, g_gameover_sheet, g_hud_sheet;
/* romfs:/ui/hud.t3x (tools/build_ui_assets.py): 0 note_body, 1 alpha_circle_glow, 2 note_detector_outline_64, 3 bb_direction_arrow,
 * 4 bb_direction_arrow_outline (64 px white cells, tinted per lane) */
static Tex3DS_SubTexture g_st_hud[5];
enum { HUD_BODY = 0, HUD_GLOW, HUD_OUTLINE, HUD_ARROW, HUD_ARROW_OUTLINE };
static Tex3DS_SubTexture g_st_disc, g_st_heart, g_st_ring, g_st_checker, g_st_gameover;
static C2D_Image g_img_disc, g_img_heart, g_img_ring, g_img_checker, g_img_gameover;
static bool g_ui_ok;

static void set_sub(Tex3DS_SubTexture* st, float x, float y, float w, float h, float tw, float th) {
    st->width = (u16)w;
    st->height = (u16)h;
    st->left = x / tw;
    st->right = (x + w) / tw;
    st->top = 1.0f - y / th;
    st->bottom = 1.0f - (y + h) / th;
}

bool bb_view_ui_init(void) {
    g_ui_sheet = C2D_SpriteSheetLoad("romfs:/gfx/ui.t3x");
    g_checker_sheet = C2D_SpriteSheetLoad("romfs:/gfx/checker.t3x");
    if (!g_ui_sheet || !g_checker_sheet) {
        g_ui_ok = false;
        return false;
    }
    C3D_Tex* ui = C2D_SpriteSheetGetImage(g_ui_sheet, 0).tex;
    C3D_TexSetFilter(ui, GPU_LINEAR, GPU_LINEAR);
    set_sub(&g_st_disc, 0, 0, 32, 32, 64, 64);
    set_sub(&g_st_heart, 32, 0, 32, 32, 64, 64);
    set_sub(&g_st_ring, 0, 32, 32, 32, 64, 64);
    g_img_disc.tex = g_img_heart.tex = g_img_ring.tex = ui;
    g_img_disc.subtex = &g_st_disc;
    g_img_heart.subtex = &g_st_heart;
    g_img_ring.subtex = &g_st_ring;
    C3D_Tex* ck = C2D_SpriteSheetGetImage(g_checker_sheet, 0).tex;
    C3D_TexSetFilter(ck, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(ck, GPU_REPEAT, GPU_REPEAT);
    g_img_checker.tex = ck;
    g_img_checker.subtex = &g_st_checker;
    g_hud_sheet = C2D_SpriteSheetLoad("romfs:/ui/hud.t3x");
    if (g_hud_sheet) {
        C3D_TexSetFilter(C2D_SpriteSheetGetImage(g_hud_sheet, 0).tex, GPU_LINEAR, GPU_LINEAR);
        for (int i = 0; i < 5; i++) set_sub(&g_st_hud[i], (float)(i * 64), 0, 64, 64, 512, 64);
    }
    g_ui_ok = true;
    return true;
}

void bb_view_ui_shutdown(void) {
    if (g_ui_sheet) C2D_SpriteSheetFree(g_ui_sheet);
    if (g_checker_sheet) C2D_SpriteSheetFree(g_checker_sheet);
    if (g_gameover_sheet) C2D_SpriteSheetFree(g_gameover_sheet);
    if (g_hud_sheet) C2D_SpriteSheetFree(g_hud_sheet);
    g_ui_sheet = g_checker_sheet = g_gameover_sheet = g_hud_sheet = NULL;
    g_ui_ok = false;
}

/* Per-stage game-over artwork (game_over.t3x: 400x240 cover-fit inside a 512x256 texture). */
void bb_view_set_gameover(const char* path) {
    if (g_gameover_sheet) {
        C2D_SpriteSheetFree(g_gameover_sheet);
        g_gameover_sheet = NULL;
    }
    g_gameover_sheet = C2D_SpriteSheetLoad(path);
    if (g_gameover_sheet) {
        C3D_Tex* t = C2D_SpriteSheetGetImage(g_gameover_sheet, 0).tex;
        C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
        set_sub(&g_st_gameover, 0, 0, 400, 240, 512, 256);
        g_img_gameover.tex = t;
        g_img_gameover.subtex = &g_st_gameover;
    }
}

/* Draw a tinted silhouette sprite centred at (cx,cy). */
static void sprite(const C2D_Image* img, float cx, float cy, float w, float h, u32 color, float z) {
    C2D_DrawParams dp;
    dp.pos.x = cx - w * 0.5f;
    dp.pos.y = cy - h * 0.5f;
    dp.pos.w = w;
    dp.pos.h = h;
    dp.center.x = 0.0f;
    dp.center.y = 0.0f;
    dp.depth = z;
    dp.angle = 0.0f;
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, color, 1.0f);
    C2D_DrawImage(*img, &dp, &tint);
}

void bb_view_init(bb_view_t* v, bb_anim_t* anim) {
    memset(v, 0, sizeof(*v));
    v->anim = anim;
    v->sheet_idx = -1;
    v->loaded_next_idx = -1;
    v->pending_sheet = -1;
    v->cam_scale = 1.0f;
}

static int sheet_for_loop(bb_view_t* v, const bb_rhythm_core_t* core, uint32_t loop_idx) {
    if (loop_idx >= core->chart.loop_count) {
        return -1;
    }
    return bb_anim_find(v->anim, core->chart.loops[loop_idx].name);
}

static void ensure_group(bb_view_t* v, int idx) {
    if (idx < 0) {
        return;
    }
    if (!bb_anim_load(v->anim, idx)) {
        v->sheet_loads_failed++;
    }
    for (int k = 0; k < v->anim->sheet[idx].var_count; k++) {
        if (!bb_anim_load(v->anim, v->anim->sheet[idx].var_idx[k])) {
            v->sheet_loads_failed++;
        }
    }
}

static bool group_resident(const bb_view_t* v, int idx) {
    const bb_anim_sheet_t* sh = &v->anim->sheet[idx];
    for (int k = 0; k < sh->var_count; k++) {
        if (!v->anim->sheet[sh->var_idx[k]].resident) return false;
    }
    return sh->resident;
}

static void unload_group(bb_view_t* v, int idx) {
    if (idx < 0) {
        return;
    }
    for (int k = 0; k < v->anim->sheet[idx].var_count; k++) {
        bb_anim_unload(v->anim, v->anim->sheet[idx].var_idx[k]);
    }
    bb_anim_unload(v->anim, idx);
}

void bb_view_reset(bb_view_t* v, const bb_rhythm_core_t* core) {
    bb_anim_t* anim = v->anim;
    bb_video_t* video = v->video;
    int video_count = v->video_count;
    if (!anim->keep_all) {
        for (uint32_t i = 0; i < anim->sheet_count; i++) {
            bb_anim_unload(anim, (int)i);
        }
    }
    memset(v, 0, sizeof(*v));
    v->anim = anim;
    v->video = video;
    v->video_count = video_count;
    v->sheet_idx = sheet_for_loop(v, core, 0);
    v->loaded_next_idx = -1;
    v->pending_sheet = -1;
    v->cam_scale = 1.0f;
    v->cam_from_s = v->cam_to_s = 1.0f;
    v->gray = 0.0f;
    v->pulse_t0_us = -100000000LL;
    v->shake_t0_us = -100000000LL;
    v->shutter_t0_us = -100000000LL;
    v->judge_t0_us = -100000000LL;
    g_held_lanes = 0;
    for (int l = 0; l < 4; l++) g_press_t[l] = -10.0f;
    g_judge_t = g_score_pulse_t = g_star_pulse_t = -10.0f;
    for (int i = 0; i < 5; i++) g_star_refill_t[i] = -10.0f;
    g_stars_active = 5;
    v->draw_sheet = v->sheet_idx;
    v->rng = 0x9E3779B9u;
    ensure_group(v, v->sheet_idx);
    v->loaded_next_idx = sheet_for_loop(v, core, 1);
    if (!anim->keep_all) {
        ensure_group(v, v->loaded_next_idx); /* synchronous: no file-system contention with the audio stream during play */
    }
}

/* Make next_sheet (loop loop_idx) the drawn sheet; in the rolling window the old group is freed and the one after is
 * trickled in by bb_view_tick. */
static void switch_sheet(bb_view_t* v, const bb_rhythm_core_t* core, int next_sheet, uint32_t loop_idx) {
    if (next_sheet < 0 || next_sheet == v->sheet_idx) {
        return;
    }
    if (!group_resident(v, next_sheet)) {
        ensure_group(v, next_sheet);   /* bb_anim_load first waits for any page in flight, so only when it is needed */
    }
    int old = v->sheet_idx;
    v->sheet_idx = next_sheet;
    v->draw_sheet = next_sheet;
    if (!v->anim->keep_all) {
        if (old >= 0) {
            unload_group(v, old);
        }
        v->loaded_next_idx = sheet_for_loop(v, core, loop_idx + 1);
    }
}

static uint32_t sheet_total(const bb_view_t* v) {
    if (v->sheet_idx < 0) return 1;
    return v->anim->sheet[v->sheet_idx].frame_count;
}

/* GameCharacter.run_loop: frame 0, then linear tween to total-1 over D * (1 - 1/total) */
/* Config.pick_variation_index: roll = rand * max(100, total weight); first bucket the roll falls into wins */
static void try_variation(bb_view_t* v) {
    v->draw_sheet = v->sheet_idx;
    if (v->sheet_idx < 0) {
        return;
    }
    const bb_anim_sheet_t* sh = &v->anim->sheet[v->sheet_idx];
    if (sh->var_count == 0) {
        return;
    }
    float total = 0.0f;
    for (int k = 0; k < sh->var_count; k++) total += sh->var_weight[k];
    v->rng = v->rng * 1664525u + 1013904223u;
    float roll = (float)(v->rng >> 8) / 16777216.0f * (total > 100.0f ? total : 100.0f);
    for (int k = 0; k < sh->var_count; k++) {
        roll -= sh->var_weight[k];
        if (roll < 0.0f) {
            v->draw_sheet = sh->var_idx[k];
            return;
        }
    }
}

static void run_loop(bb_view_t* v, int64_t now_us) {
    try_variation(v);
    v->frame = 0;
    v->tween_active = false;
    if (v->anim_duration_us <= 0) {
        return;
    }
    uint32_t total = sheet_total(v);
    v->tween_t0_us = now_us;
    v->tween_dur_us = v->anim_duration_us - v->anim_duration_us / (int64_t)total;
    v->tween_active = v->tween_dur_us > 0;
}

static void current_frame_update(bb_view_t* v, int64_t now_us) {
    if (!v->tween_active) {
        return;
    }
    uint32_t total = sheet_total(v);
    int64_t el = now_us - v->tween_t0_us;
    if (el >= v->tween_dur_us) {
        v->frame = total - 1;
        v->tween_active = false;
        return;
    }
    if (el < 0) el = 0;
    float f = (float)el / (float)v->tween_dur_us * (float)(total - 1);
    v->frame = (uint32_t)f;
}

static void camera_update(bb_view_t* v, int64_t now_us) {
    const bb_anim_t* a = v->anim;
    while (v->cam_next_key < a->camera_count && now_us >= a->camera[v->cam_next_key].timestamp_us) {
        const bb_anim_camera_key_t* k = &a->camera[v->cam_next_key];
        v->cam_from_x = v->cam_pos_x;
        v->cam_from_y = v->cam_pos_y;
        v->cam_from_s = v->cam_scale;
        v->cam_to_x = k->pos_x;
        v->cam_to_y = k->pos_y;
        v->cam_to_s = k->scale_x;
        v->cam_t0_us = k->timestamp_us;
        v->cam_dur_s = k->duration_s;
        v->cam_next_key++;
    }
    if (v->cam_dur_s <= 0.0f) {
        v->cam_pos_x = v->cam_to_x; v->cam_pos_y = v->cam_to_y; v->cam_scale = v->cam_to_s;
        return;
    }
    float t = (float)(now_us - v->cam_t0_us) / 1000000.0f / v->cam_dur_s;
    float e = expo_out(clampf(t, 0.0f, 1.0f));
    v->cam_pos_x = v->cam_from_x + (v->cam_to_x - v->cam_from_x) * e;
    v->cam_pos_y = v->cam_from_y + (v->cam_to_y - v->cam_from_y) * e;
    v->cam_scale = v->cam_from_s + (v->cam_to_s - v->cam_from_s) * e;
}

void bb_view_event(bb_view_t* v, const bb_rhythm_core_t* core, const bb_event_t* ev) {
    int64_t now = core->song_cur_time_us;
    static const char* absent_texts[3] = { "HUH?", "WHAT?", "BOO." };
    const bb_event_t e = *ev;

    if (e.type == BB_EVT_HIT || e.type == BB_EVT_MISS || e.type == BB_EVT_ABSENT) {   /* GameStars.hit_attempt: set_stars + pulse */
        const uint32_t reg = core->stats.registered, hit = core->stats.hit;
        int grade = 6;
        if (reg > 0) {
            const float acc = (float)hit / (float)reg;
            grade = acc < 0.6f ? 1 : acc < 0.7f ? 2 : acc < 0.8f ? 3 : acc < 0.9f ? 4 : acc < 1.0f ? 5 : 6;
        }
        const int active = grade > 5 ? 5 : grade;
        for (int i = g_stars_active; i < active; i++) {   /* a star refills: ripple + sfx_refill_star at pitch 0.7 + index / 10 */
            g_star_refill_t[i] = bb_ui_time();
            bb_uisfx_play_pitch(BB_UISFX_STAR, 0.7f + (float)i / 10.0f);
        }
        g_stars_active = active;
        g_star_pulse_t = bb_ui_time();
    }
    {
        switch (e.type) {
            case BB_EVT_HIT:
                v->gray = 0.0f;                       /* gray_amount = 0 on every hit */
                v->judge = e.judgment;
                v->judge_t0_us = now;
                v->life_pulse = 1.0f;
                {   /* NoteDetector._on_note_hit: points = calculate_modified_score; ScoreDisplay pulses */
                    static const int base[5] = { 0, 5, 10, 50, 100 };   /* indexed by bb_judgment_t MEH..PERFECT */
                    const int j = (int)e.judgment;
                    g_judge_points = (j >= 1 && j <= 4 ? base[j] : 0) * core->score_mult_x10 / 10;
                    g_judge_word = j == BB_JUDGE_PERFECT ? "PERFECT" : j == BB_JUDGE_GREAT ? "GREAT" : j == BB_JUDGE_NICE ? "NICE" : "OKAY";
                    g_judge_red = false;
                    g_judge_t = bb_ui_time();
                    g_score_pulse_t = bb_ui_time();
                }
                if (!(e.flags & BB_EVTF_IGNORE_ANIM)) {
                    v->pulse_t0_us = now;             /* pulse_character */
                    v->anim_duration_us = bb_core_time_til_next_note_us(core, e.note_idx);
                    run_loop(v, now);
                }
                break;
            case BB_EVT_MISS:
                v->gray = 1.0f;
                v->shake_t0_us = now;
                v->judge = BB_JUDGE_PASSED;
                v->judge_t0_us = now;
                g_judge_points = 0;   /* "+0" and a random miss word (set_note_accuracy_text_miss) */
                g_judge_word = absent_texts[v->absent_rot++ % 3];
                g_judge_red = true;
                g_judge_t = bb_ui_time();
                break;
            case BB_EVT_ABSENT:
                v->tween_active = false;              /* loop_tween.kill(): hold current frame */
                v->gray = 1.0f;
                v->shake_t0_us = now;
                v->judge = BB_JUDGE_ABSENT;
                v->judge_t0_us = now;
                v->absent_text = absent_texts[v->absent_rot++ % 3];
                g_judge_points = -20;
                g_judge_word = v->absent_text;
                g_judge_red = true;
                g_judge_t = bb_ui_time();
                g_score_pulse_t = bb_ui_time();
                break;
            case BB_EVT_SHUTTER:
                v->shutter_t0_us = now;
                v->shutter_started = true;
                break;
            case BB_EVT_LOOP: {
                int next_sheet = sheet_for_loop(v, core, e.note_idx);
                if (!v->anim->keep_all && next_sheet >= 0 && next_sheet != v->sheet_idx && !group_resident(v, next_sheet)) {
                    /* Rolling window and the sheet has not streamed in yet (two loop changes close together): a load
                     * here would stall the frame and the audio stream for 100-300 ms on an Old 3DS, so the current
                     * animation keeps playing and bb_view_tick switches as soon as the page worker has it. */
                    if (v->pending_sheet >= 0 && v->pending_sheet != next_sheet) {
                        unload_group(v, v->pending_sheet);   /* overtaken by a later loop */
                    }
                    v->pending_sheet = next_sheet;
                    run_loop(v, now);
                    break;
                }
                if (v->pending_sheet >= 0 && v->pending_sheet != next_sheet) {
                    unload_group(v, v->pending_sheet);
                }
                v->pending_sheet = -1;
                switch_sheet(v, core, next_sheet, e.note_idx);
                run_loop(v, now); /* uses the previous animation_duration, as the reference does */
                break;
            }
            default:
                break;
        }
    }
}

/* Rolling-window stages: bring the NEXT loop's sheet group in on the page worker, a page at a time, so no frame waits on a
 * texture decompress while the song plays (a page on the main thread was a 60-100 ms hitch). */
static void trickle_next_group(bb_view_t* v, const bb_rhythm_core_t* core) {
    if (v->anim->keep_all || core->state != BB_STATE_PLAYING) {
        return;
    }
    if (v->pending_sheet >= 0) {   /* the loop that already began comes first; switch to it once it is in */
        const int p = v->pending_sheet;
        if (!bb_anim_load_step(v->anim, p)) {
            return;
        }
        for (int k = 0; k < v->anim->sheet[p].var_count; k++) {
            if (!bb_anim_load_step(v->anim, v->anim->sheet[p].var_idx[k])) {
                return;
            }
        }
        v->pending_sheet = -1;
        switch_sheet(v, core, p, core->current_loop_idx);
        run_loop(v, core->song_cur_time_us);
        return;
    }
    int nxt = sheet_for_loop(v, core, core->current_loop_idx + 1);
    if (nxt < 0) {
        return;
    }
    if (!bb_anim_load_step(v->anim, nxt)) {
        return;
    }
    for (int k = 0; k < v->anim->sheet[nxt].var_count; k++) {
        if (!bb_anim_load_step(v->anim, v->anim->sheet[nxt].var_idx[k])) {
            return;
        }
    }
}

void bb_view_tick(bb_view_t* v, const bb_rhythm_core_t* core) {
    int64_t now = core->song_cur_time_us;
    trickle_next_group(v, core);
    current_frame_update(v, now);
    camera_update(v, now);
    if (v->life_pulse > 0.0f) {
        v->life_pulse -= 0.08f;
        if (v->life_pulse < 0.0f) v->life_pulse = 0.0f;
    }
}

static void draw_checker(int64_t now_us) {
    /* CheckerBackground: 64 canvas-px cells scrolling upward at 2 cells/s; one repeating 2x2-cell tile */
    C2D_DrawRectSolid(0, 0, Z_BG, BB_VIEW_W, BB_VIEW_H, C2D_Color32(6, 6, 7, 0xFF));
    if (!g_ui_ok) {
        return;
    }
    const float tile_px = 128.0f * K;                       /* 2 cells */
    float scroll = fmodf((float)now_us / 1000000.0f, 1.0f); /* tiles scrolled */
    g_st_checker.width = 32;
    g_st_checker.height = 32;
    g_st_checker.left = 0.0f;
    g_st_checker.right = BB_VIEW_W / tile_px;
    g_st_checker.top = 1.0f + scroll;
    g_st_checker.bottom = 1.0f + scroll - BB_VIEW_H / tile_px;
    C2D_DrawParams dp;
    dp.pos.x = 0.0f;
    dp.pos.y = 0.0f;
    dp.pos.w = BB_VIEW_W;
    dp.pos.h = BB_VIEW_H;
    dp.center.x = 0.0f;
    dp.center.y = 0.0f;
    dp.depth = Z_BG + 0.01f;
    dp.angle = 0.0f;
    C2D_DrawImage(g_img_checker, &dp, NULL);
}

static void draw_character(const bb_view_t* v, int64_t now_us) {
    if (v->sheet_idx < 0) {
        return;
    }
    const C2D_Image* img = bb_anim_frame(v->anim, v->draw_sheet, v->frame);
    if (!img) {
        img = bb_anim_frame(v->anim, v->sheet_idx, v->frame);
    }
    if (!img) {
        return;
    }
    if (g_book) {   /* the art's span of the frame, turned a quarter right: upright when the 3DS is held turned a quarter left */
        static Tex3DS_SubTexture st;
        st = *img->subtex;
        const float l = st.left, r = st.right;
        st.left = l + (r - l) * g_book_u0;
        st.right = l + (r - l) * g_book_u1;
        st.width = (u16)((float)img->subtex->width * (g_book_u1 - g_book_u0));
        C2D_Image part = { img->tex, &st };
        const float art_w = 1280.0f * (g_book_u1 - g_book_u0), art_h = 720.0f;
        float scale = 240.0f / art_w;                 /* the held view is 240 wide, 400 tall */
        if (art_h * scale > 400.0f) scale = 400.0f / art_h;
        const float el = (float)(now_us - v->pulse_t0_us) / 1000000.0f;
        float pulse = 1.0f;
        if (el >= 0.0f && el < 0.3f) {
            const float t = el / 0.3f, ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
            pulse = 1.1f + (1.0f - 1.1f) * ease;
        }
        const float w = art_w * scale * pulse, h = art_h * scale * pulse;
        /* the art stands on the bottom of the held view (the screen's left edge); room above the head */
        C2D_DrawParams bp = { { h * 0.5f, 120.0f, w, h }, { w * 0.5f, h * 0.5f }, Z_CHAR, 1.5708f };
        C2D_ImageTint bt;
        C2D_PlainImageTint(&bt, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), 0.0f);
        if (v->gray > 0.0f) {
            C2D_SetTintMode(C2D_TintLuma);
            C2D_PlainImageTint(&bt, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), v->gray);
            C2D_DrawImage(part, &bp, &bt);
            C2D_SetTintMode(C2D_TintMult);
        } else {
            C2D_DrawImage(part, &bp, NULL);
        }
        return;
    }
    float pulse = 1.0f;
    float el = (float)(now_us - v->pulse_t0_us) / 1000000.0f;
    if (el >= 0.0f && el < 0.3f) {
        float t = el / 0.3f;
        float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); /* cubic out */
        pulse = 1.1f + (1.0f - 1.1f) * ease;
    }
    float shake_x = 0.0f;
    float sel = (float)(now_us - v->shake_t0_us) / 1000000.0f;
    if (sel >= 0.0f && sel < 0.9f) {
        static const float keys[9] = { 5.0f, -4.0f, 3.0f, -2.0f, 1.0f, -0.5f, 0.25f, -0.125f, 0.0f };
        shake_x = keys[(int)(sel / 0.1f)];
    }

    float s = v->cam_scale;
    /* frame center on screen */
    float cx = 200.0f + CHAR_SCALE * K * (v->cam_pos_x + shake_x * s);
    float cy = 120.0f + CHAR_SCALE * K * v->cam_pos_y;
    float w = 1280.0f * CHAR_SCALE * K * s * pulse;
    float h = 720.0f * CHAR_SCALE * K * s * pulse;

    C2D_DrawParams p;
    p.pos.x = cx - w * 0.5f;
    p.pos.y = cy - h * 0.5f;
    p.pos.w = w;
    p.pos.h = h;
    p.center.x = 0.0f;
    p.center.y = 0.0f;
    p.depth = Z_CHAR;
    p.angle = 0.0f;

    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), 0.0f);
    if (v->gray > 0.0f) {
        C2D_SetTintMode(C2D_TintLuma);
        C2D_PlainImageTint(&tint, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), v->gray);
        C2D_DrawImage(*img, &p, &tint);
        C2D_SetTintMode(C2D_TintMult);
    } else {
        C2D_DrawImage(*img, &p, NULL);
    }
}

static void draw_heart(float x, float y, float r, u32 col) {
    if (bb_theme_halloween()) {   /* Heart.gd set_theme_visuals: game_heart_drippy, heart and shadow #51EE2E */
        const bool full = (col >> 24) == 0xFF;
        bb_art_hw_cell(0, x, y + 1.0f, r * 2.6f, 0.0f, full ? C2D_Color32(0x51, 0xEE, 0x2E, 0xFF) : C2D_Color32(0x1C, 0x50, 0x10, 0x90), Z_HUD);
        return;
    }
    if (g_ui_ok) {
        sprite(&g_img_heart, x, y + 1.0f, r * 2.4f, r * 2.4f, col, Z_HUD);
    }
}

/* ---------------- bottom screen: 320x240 four-lane highway + rhythm HUD ---------------- */
#define BOT_W        320.0f
#define BOT_LANE_GAP 64.0f
#define BOT_HIT_Y    204.0f
#define BOT_NOTE_R   13.0f

static float bot_lane_x(uint8_t input_type) {
    return BOT_W * 0.5f + ((float)k_lane_slot[input_type & 3] - 1.5f) * BOT_LANE_GAP;
}

/* Scroll Mode (Settings > Gameplay): DOWN = notes fall to the hit line at the bottom (the original's downscroll); UP = they rise to a
 * hit line under the HUD; SIDE = four horizontal rows, notes travel right to left to receptors on the left. Four quadrants in all. */
static int g_scroll = BB_SCROLL_DOWN;
void bb_view_set_scroll_mode(int mode) { g_scroll = (mode >= 0 && mode < BB_SCROLL_MODE_COUNT) ? mode : BB_SCROLL_DOWN; }
#define UP_HIT_Y    36.0f   /* the HUD moves to the bottom in this mode */
#define SIDE_HIT_X  34.0f
#define SIDE_TOP_N  50.0f
#define SIDE_ROW_N  47.0f
#define BOOK_HUD    72.0f
#define SIDE_TOP    (g_book ? BOOK_HUD : SIDE_TOP_N)
#define SIDE_ROW    (g_book ? 41.5f : SIDE_ROW_N)
static void receptor_pos(int t, float* x, float* y) {
    const int slot = k_lane_slot[t & 3];
    if (g_scroll == BB_SCROLL_SIDE) { *x = SIDE_HIT_X; *y = SIDE_TOP + SIDE_ROW * ((float)slot + 0.5f); return; }
    *x = bot_lane_x((uint8_t)t);
    *y = g_scroll == BB_SCROLL_UP ? UP_HIT_Y : BOT_HIT_Y;
}
/* a point `dist` pixels before the receptor along the lane (positive = still to come) */
static void lane_point(int t, float dist, float* x, float* y) {
    receptor_pos(t, x, y);
    if (g_scroll == BB_SCROLL_SIDE) *x += dist;
    else if (g_scroll == BB_SCROLL_UP) *y += dist;
    else *y -= dist;
}
/* how far a note can be before the receptor and still be on screen */
static float lane_reach(void) {
    return g_scroll == BB_SCROLL_SIDE ? BOT_W - SIDE_HIT_X + 20.0f : (g_scroll == BB_SCROLL_UP ? BB_VIEW_H - UP_HIT_Y + 20.0f : BOT_HIT_Y + 20.0f);
}
int bb_view_touch_lane(int px, int py) {
    if (g_scroll == BB_SCROLL_SIDE) {
        const int slot = (int)(((float)py - SIDE_TOP) / SIDE_ROW);
        return ((float)py >= SIDE_TOP && slot >= 0 && slot < 4) ? bb_view_lane_at_slot(slot) : -1;
    }
    const int slot = (px - 32) / 64;
    return (px >= 32 && slot >= 0 && slot < 4) ? bb_view_lane_at_slot(slot) : -1;
}

/* The Blindfold power-up (class/powerups.gd "disappearing"): notes fade out as they approach the hit line. */
static bool g_blindfold = false;
void bb_view_set_blindfold(bool on) { g_blindfold = on; }
/* Modifiers menu: No HUD (hearts/score/combo/judgment/progress) and No Beat Bar (receptors), GameManager.no_hud / no_beat_bar. */
static bool g_no_hud = false, g_no_beat_bar = false;
void bb_view_set_modifiers(bool no_hud, bool no_beat_bar) { g_no_hud = no_hud; g_no_beat_bar = no_beat_bar; }

static u32 scale_alpha(u32 col, float a) {
    u32 na = (u32)((float)(col >> 24) * a);
    return (col & 0x00FFFFFFu) | (na << 24);
}

static void hud_sprite(int cell, float cx, float cy, float size, float angle, u32 tint, float z) {
    if (!g_hud_sheet) return;
    C2D_Image img = C2D_SpriteSheetGetImage(g_hud_sheet, 0);
    img.subtex = &g_st_hud[cell];
    C2D_DrawParams dp;
    dp.pos.x = cx; dp.pos.y = cy; dp.pos.w = size; dp.pos.h = size;
    dp.center.x = size * 0.5f; dp.center.y = size * 0.5f; dp.depth = z; dp.angle = angle;
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, tint, 1.0f);
    C2D_SetTintMode(C2D_TintMult);
    C2D_DrawImage(img, &dp, &t);
}

/* Keybinds.get_directional_rotation for the D-Pad lanes (input types 0..3 = down, right, left, up; the art points up) */
/* the D-pad direction that plays lane t; the book layout turns the buttons a quarter with the console (main.c book_*_lanes):
 * lanes B / A / Y / X are played by Y / B / X / A and Left / Down / Up / Right */
static float lane_arrow_angle(int t) {
    static const float a[4] = { 3.14159f, 1.5708f, -1.5708f, 0.0f }, book[4] = { -1.5708f, 3.14159f, 0.0f, 1.5708f };
    return g_book ? book[t & 3] : a[t & 3];
}

static void draw_notes_bottom(const bb_rhythm_core_t* core, int64_t now_us, float speed, int ctrl_mode) {
    const float cur_s = (float)now_us / 1000000.0f;
    const bool arrows = ctrl_mode == BB_CTRL_DPAD;
    const float D = BOT_NOTE_R * 2.0f + 2.0f;
    const float reach = lane_reach();
    for (uint32_t i = 0; i < core->chart.note_count; i++) {
        const bb_note_t* n = &core->chart.notes[i];
        float dist = ((float)n->timestamp_us / 1000000.0f - cur_s) * speed;   /* pixels before the receptor */
        if (dist > reach && n->modifier != BB_MOD_HOLD) break;   /* sorted: everything later is off screen */
        float bf = 1.0f;
        if (g_blindfold) {
            float d = dist / (238.5f * K);   /* 2 = full alpha (477 px), 1 = invisible (half) */
            bf = clampf(d - 1.0f, 0.0f, 1.0f);
        }
        if (n->modifier == BB_MOD_HOLD) {
            float tail = ((float)n->hold_end_us / 1000000.0f - cur_s) * speed;
            if (n->holding) dist = 0.0f;
            if (dist > reach) break;
            if (tail < -10.0f) continue;
            if ((n->active || n->holding) && bf > 0.0f) {
                const float far = tail > reach + 8.0f ? reach + 8.0f : tail;
                if (far > dist) {
                    float hx, hy, tx2, ty2;
                    lane_point(n->input_type, dist, &hx, &hy);
                    lane_point(n->input_type, far, &tx2, &ty2);
                    const u32 tc = scale_alpha(lane_color(n->input_type, n->holding ? 0xE0 : 0xB0), bf);
                    if (g_scroll == BB_SCROLL_SIDE) C2D_DrawRectSolid(hx, hy - 5.0f, Z_NOTE, tx2 - hx, 10.0f, tc);
                    else C2D_DrawRectSolid(hx - 5.0f, hy < ty2 ? hy : ty2, Z_NOTE, 10.0f, fabsf(ty2 - hy), tc);
                }
            }
        }
        if (dist < -BOT_NOTE_R - 30.0f) continue;   /* passed the receptor and gone */
        float x, y;
        lane_point(n->input_type, dist, &x, &y);
        if (!n->active && n->result != BB_JUDGE_PASSED) continue; /* hit: gone */
        float a = bf;
        u32 body = lane_color(n->input_type, 0xFF);
        if (n->result == BB_JUDGE_PASSED) { body = C2D_Color32(0x33, 0x00, 0x00, 0xFF); a *= 0.5f; }   /* modulate (0.2, 0, 0, 0.5) */
        else if (n->modifier == BB_MOD_GHOST) a *= 0.33f;
        if (a <= 0.0f) continue;
        if (!g_hud_sheet) {
            if (g_ui_ok) sprite(&g_img_disc, x, y, BOT_NOTE_R * 2.0f, BOT_NOTE_R * 2.0f, scale_alpha(body, a), Z_NOTE + 0.02f);
            continue;
        }
        hud_sprite(HUD_GLOW, x, y, D * 2.2f, 0.0f, scale_alpha(lane_color(n->input_type, 0xFF), 0.2f * a), Z_NOTE);   /* Glow (downscroll) */
        if (arrows) {
            hud_sprite(HUD_ARROW, x, y, D, lane_arrow_angle(n->input_type), scale_alpha(body, a), Z_NOTE + 0.02f);
        } else {
            hud_sprite(HUD_BODY, x, y + 1.5f, D + 2.0f, 0.0f, C2D_Color32(0x1A, 0x1A, 0x1A, (u8)(255.0f * a)), Z_NOTE + 0.01f);   /* NoteShadow */
            const u8 br = 0xE0;   /* NoteBody self_modulate 0.88 */
            const u32 b2 = C2D_Color32((u8)(((body) & 0xFF) * br / 255), (u8)(((body >> 8) & 0xFF) * br / 255), (u8)(((body >> 16) & 0xFF) * br / 255), (u8)(255.0f * a));
            hud_sprite(HUD_BODY, x, y, D, 0.0f, b2, Z_NOTE + 0.02f);
        }
    }
}

static float ease_out_bounce(float t) {
    t = clampf(t, 0.0f, 1.0f);
    const float n1 = 7.5625f, d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}

/* Book layout HUD: drawn in the held frame (240 wide x 320 tall, top = the screen's right edge) inside the 72 px column on the left
 * of the hand-held view: progress, score + multiplier, hearts, stars, then the judgement pop-up. */
static void draw_book_hud(const bb_rhythm_core_t* core, int64_t now, float ut) {
    char buf[48];
    C3D_Mtx saved;
    C2D_ViewSave(&saved);
    C2D_ViewTranslate(BOT_W, 0.0f);   /* held frame (u, v) -> screen (320 - v, u) */
    C2D_ViewRotate(1.5708f);
    const float cx = BOOK_HUD * 0.5f;
    C2D_DrawRectSolid(0, 0, Z_HUD - 0.01f, BOOK_HUD, BOT_W, C2D_Color32(0x0A, 0x0A, 0x12, 0xB0));
    if (!g_no_hud) {
        if (core->chart.note_count > 0) {   /* progress, along the column's top */
            float last = (float)core->chart.notes[core->chart.note_count - 1].timestamp_us;
            float f = clampf((float)now / last, 0.0f, 1.0f);
            C2D_DrawRectSolid(4.0f, 4.0f, Z_HUD, BOOK_HUD - 8.0f, 3.0f, C2D_Color32(0, 0, 0, 0x90));
            C2D_DrawRectSolid(4.0f, 4.0f, Z_HUD + 0.01f, (BOOK_HUD - 8.0f) * f, 3.0f, C2D_Color32(0xFF, 0x4A, 0x8A, 0xFF));
        }
        const float pe = ut - g_score_pulse_t;
        const float sc = pe >= 0.0f && pe < 0.5f ? 1.1f - 0.1f * (pe / 0.5f) : 1.0f;
        snprintf(buf, sizeof(buf), "%06ld", (long)core->stats.score);
        bb_font_draw_xf(BB_FACE_HEAD, 16, cx, 22.0f + 1.5f, sc, 0.0f, C2D_Color32(0x38, 0x38, 0x38, 0xFF), buf, Z_HUD + 0.02f);
        bb_font_draw_xf(BB_FACE_HEAD, 16, cx, 22.0f, sc, 0.0f, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), buf, Z_HUD + 0.03f);
        snprintf(buf, sizeof(buf), "x%.1f", (double)core->score_mult_x10 / 10.0);
        bb_font_draw(BB_FACE_BOLD, 12, cx, 34.0f, C2D_Color32(0xFF, 0x90, 0x00, 0xFF), C2D_AlignCenter, buf, Z_HUD + 0.03f);
        for (int32_t h = 0; h < core->stats.max_health; h++) {   /* hearts, four to a row */
            u32 col = (h < core->stats.health) ? C2D_Color32(0xFF, 0x3B, 0x5C, 0xFF) : C2D_Color32(0x40, 0x20, 0x28, 0x90);
            draw_heart(12.0f + (float)(h % 4) * 16.0f, 64.0f + (float)(h / 4) * 15.0f, 6.0f, col);
        }
        const float sp = ut - g_star_pulse_t;
        const float pulse = sp >= 0.0f && sp < 0.5f ? 1.1f - 0.1f * (sp / 0.5f) : 1.0f;
        for (int i = 0; i < 5; i++) {
            const float sx = 10.0f + (float)i * 13.0f, sy = 100.0f;
            const bool on = i < g_stars_active;
            if (bb_theme_halloween()) bb_art_hw_cell(on ? 2 : 1, sx, sy, 13.0f * pulse, 0.0f, 0xFFFFFFFFu, Z_HUD + 0.01f);
            else bb_art_star(!on, sx, sy, 12.0f * pulse, 0xFFFFFFFFu, Z_HUD + 0.01f);
        }
    }
    const float je = ut - g_judge_t;
    if (!g_no_beat_bar && je >= 0.0f && je < 1.0f && g_judge_word) {
        const float sc = 0.85f * (1.0f + 0.5f * (1.0f - bb_ui_ease_out_back(je / 0.4f)));
        const float a = 1.0f - je, cy = 180.0f;
        const u32 jc = g_judge_red ? C2D_Color32(0xE6, 0x17, 0x2B, (u8)(255.0f * a)) : C2D_Color32(0xFF, 0xD8, 0x3A, (u8)(255.0f * a));
        bb_font_draw_xf(BB_FACE_NOTO, 17, cx, cy, sc, 0.0f, jc, g_judge_word, Z_HUD + 0.04f);
        if (core->stats.combo >= 10) {
            snprintf(buf, sizeof(buf), "x%lu", (unsigned long)core->stats.combo);
            bb_font_draw_xf(BB_FACE_NOTO, 14, cx, cy - 16.0f * sc, sc, 0.0f, C2D_Color32(0xF0, 0xB8, 0x00, (u8)(255.0f * a)), buf, Z_HUD + 0.04f);
        }
        if (g_judge_points < 0) snprintf(buf, sizeof(buf), "%d", g_judge_points);
        else snprintf(buf, sizeof(buf), "+%d", g_judge_points);
        bb_font_draw_xf(BB_FACE_NOTO, 14, cx, cy + 16.0f * sc, sc, 0.0f, C2D_Color32(0xFF, 0xFF, 0xFF, (u8)(255.0f * a)), buf, Z_HUD + 0.04f);
    }
    C2D_ViewRestore(&saved);
}

void bb_view_draw_bottom(bb_view_t* v, const bb_rhythm_core_t* core, C2D_TextBuf tb, int ctrl_mode, float scroll_mult) {
    int64_t now = core->song_cur_time_us;
    char buf[48];
    const float ut = bb_ui_time();
    (void)tb; (void)v;
    g_text_z = Z_HUD;

    /* the top screen's checkerboard behind the highway (same clock: both screens scroll together); the lane panel is translucent
     * over it, so the checker shows through the four quadrants and fills the margins beside them */
    draw_checker(now);
    /* highway panel + lane separators (the four quadrants: columns, or rows in side scroll) */
    if (g_scroll == BB_SCROLL_SIDE) {
        C2D_DrawRectSolid(0, SIDE_TOP, Z_LANE, BOT_W, SIDE_ROW * 4.0f, C2D_Color32(0x16, 0x16, 0x22, 0x8C));
        for (int k = 0; k < 5; k++) C2D_DrawRectSolid(0, SIDE_TOP + (float)k * SIDE_ROW - 0.5f, Z_LANE + 0.005f, BOT_W, 1.0f, C2D_Color32(0x30, 0x30, 0x48, 0xFF));
    } else {
        float px0 = bot_lane_x(2) - BOT_LANE_GAP * 0.5f, px1 = bot_lane_x(1) + BOT_LANE_GAP * 0.5f;
        C2D_DrawRectSolid(px0, 0, Z_LANE, px1 - px0, BB_VIEW_H, C2D_Color32(0x16, 0x16, 0x22, 0x8C));   /* ~55 %: the checker shows through */
        for (int k = 0; k < 5; k++) {
            C2D_DrawRectSolid(px0 + (float)k * BOT_LANE_GAP - 0.5f, 0, Z_LANE + 0.005f, 1.0f, BB_VIEW_H, C2D_Color32(0x30, 0x30, 0x48, 0xFF));
        }
    }

    const float speed = 350.0f * K * scroll_mult;
    /* receptors (DownscrollLaneArrow): the lane colour at 0.5 alpha; full brightness only while that lane is physically held, and a
     * 1.5 -> 1.0 bounce (0.5 s) on every physical press - never from hits or autoplay */
    for (int t = 0; t < 4 && !g_no_beat_bar; t++) {
        float x, ry;
        receptor_pos(t, &x, &ry);
        const bool held = (g_held_lanes >> t) & 1;
        const float since = ut - g_press_t[t];
        const float sc = since < 0.5f ? 1.5f - 0.5f * ease_out_bounce(since / 0.5f) : 1.0f;
        const float D = (BOT_NOTE_R + 2.0f) * 2.0f * sc;
        if (held) {   /* a soft beam from the pressed receptor into its lane */
            const u32 c0 = lane_color((uint8_t)t, 0x00), c1 = lane_color((uint8_t)t, 0x3C);
            if (g_scroll == BB_SCROLL_SIDE) C2D_DrawRectangle(x, ry - SIDE_ROW * 0.5f + 1.0f, Z_LANE + 0.006f, 84.0f, SIDE_ROW - 2.0f, c1, c0, c1, c0);
            else if (g_scroll == BB_SCROLL_UP) C2D_DrawRectangle(x - BOT_LANE_GAP * 0.5f + 1.0f, ry, Z_LANE + 0.006f, BOT_LANE_GAP - 2.0f, 84.0f, c1, c1, c0, c0);
            else C2D_DrawRectangle(x - BOT_LANE_GAP * 0.5f + 1.0f, ry - 84.0f, Z_LANE + 0.006f, BOT_LANE_GAP - 2.0f, 84.0f, c0, c0, c1, c1);
            hud_sprite(HUD_GLOW, x, ry, D * 2.0f, 0.0f, lane_color((uint8_t)t, 0x70), Z_LANE + 0.008f);
        }
        const u32 rc = lane_color((uint8_t)t, held ? 0xFF : 0x80);
        if (g_hud_sheet) {
            hud_sprite(HUD_BODY, x, ry, D * 0.92f, 0.0f, C2D_Color32(0x08, 0x08, 0x0C, 0xD0), Z_LANE + 0.01f);
            if (ctrl_mode == BB_CTRL_DPAD) hud_sprite(HUD_ARROW, x, ry, D, lane_arrow_angle(t), rc, Z_LANE + 0.02f);
            else hud_sprite(HUD_OUTLINE, x, ry, D, 0.0f, rc, Z_LANE + 0.02f);
        }
        if (ctrl_mode != BB_CTRL_DPAD) {   /* which button presses the lane */
            static const char* const letters_n[4] = { "B", "A", "Y", "X" };
            static const char* const letters_book[4] = { "Y", "B", "X", "A" };
            const char* const* letters = g_book ? letters_book : letters_n;
            static const char* const shoulders[4] = { "ZR", "R", "L", "ZL" };   /* BB_CTRL_SHOULDERS: L / ZL / ZR / R play Y / X / B / A */
            const bool sh = ctrl_mode == BB_CTRL_SHOULDERS;
            if (g_book) bb_font_draw_xf(BB_FACE_NOTO, sh ? 12 : 14, x, ry, 1.0f, 1.5708f, lane_color((uint8_t)t, held ? 0xFF : 0xC0),
                                        sh ? shoulders[t] : letters[t], Z_LANE + 0.03f);
            else bb_font_draw(BB_FACE_NOTO, sh ? 12 : 14, x, ry - (sh ? 9.0f : 10.0f), lane_color((uint8_t)t, held ? 0xFF : 0xC0), C2D_AlignCenter,
                              sh ? shoulders[t] : letters[t], Z_LANE + 0.03f);
        }
    }
    draw_notes_bottom(core, now, speed, ctrl_mode);

    if (g_book) {
        draw_book_hud(core, now, ut);
        return;
    }
    /* NoteDetector AccuracyText: combo above, judgement, points below; pops 1.5 -> 1.0 (TRANS_BACK, 0.4 s) and fades out over 1 s */
    const float je = ut - g_judge_t;
    if (!g_no_beat_bar && je >= 0.0f && je < 1.0f && g_judge_word) {
        const float sc = 1.0f + 0.5f * (1.0f - bb_ui_ease_out_back(je / 0.4f));
        const float a = 1.0f - je;
        const float cy = g_scroll == BB_SCROLL_UP ? 168.0f : (g_scroll == BB_SCROLL_SIDE ? 30.0f : 141.0f);
        /* hit words in yellow (user direction; the original's are white), miss words red */
        const u32 jc = g_judge_red ? C2D_Color32(0xE6, 0x17, 0x2B, (u8)(255.0f * a)) : C2D_Color32(0xFF, 0xD8, 0x3A, (u8)(255.0f * a));
        bb_font_draw_xf(BB_FACE_NOTO, 17, BOT_W * 0.5f, cy, sc, 0.0f, jc, g_judge_word, Z_HUD + 0.04f);
        if (core->stats.combo >= 10) {   /* check_streak colours */
            const uint32_t cb = core->stats.combo;
            u32 cc = cb >= 300 ? C2D_Color32(0x7B, 0x2B, 0xBF, 0) : cb >= 200 ? C2D_Color32(0xF0, 0x3D, 0x00, 0) : cb >= 100 ? C2D_Color32(0xF0, 0x80, 0x00, 0) :
                     cb >= 50 ? C2D_Color32(0xF0, 0xB8, 0x00, 0) : C2D_Color32(0x80, 0x80, 0x80, 0);
            cc |= (u32)(255.0f * a) << 24;
            snprintf(buf, sizeof(buf), "x%lu", (unsigned long)cb);
            bb_font_draw_xf(BB_FACE_NOTO, 14, BOT_W * 0.5f, cy - 14.0f * sc, sc, 0.0f, cc, buf, Z_HUD + 0.04f);
        }
        if (g_judge_points < 0) snprintf(buf, sizeof(buf), "%d", g_judge_points);
        else snprintf(buf, sizeof(buf), "+%d", g_judge_points);
        bb_font_draw_xf(BB_FACE_NOTO, 14, BOT_W * 0.5f, cy + 14.0f * sc, sc, 0.0f, C2D_Color32(0xFF, 0xFF, 0xFF, (u8)(255.0f * a)), buf, Z_HUD + 0.04f);
    }

    if (g_no_hud) {
        return;
    }
    /* in Up mode the HUD is mirrored to the bottom edge (the receptors sit at the top) */
    const bool inv = g_scroll == BB_SCROLL_UP;
#define HUD_Y(y) (inv ? BB_VIEW_H - (y) : (y))
    /* progress bar */
    if (core->chart.note_count > 0) {
        float last = (float)core->chart.notes[core->chart.note_count - 1].timestamp_us;
        float f = clampf((float)now / last, 0.0f, 1.0f);
        const float py = inv ? BB_VIEW_H - 3.0f : 0.0f;
        C2D_DrawRectSolid(0, py, Z_HUD, BOT_W, 3.0f, C2D_Color32(0, 0, 0, 0x90));
        C2D_DrawRectSolid(0, py, Z_HUD + 0.01f, BOT_W * f, 3.0f, C2D_Color32(0xFF, 0x4A, 0x8A, 0xFF));
    }
    /* hearts (Lives), with the star meter (GameStars) under them (over them when mirrored) */
    for (int32_t h = 0; h < core->stats.max_health; h++) {
        u32 col = (h < core->stats.health) ? C2D_Color32(0xFF, 0x3B, 0x5C, 0xFF) : C2D_Color32(0x40, 0x20, 0x28, 0x90);
        draw_heart(14.0f + (float)h * 17.0f, HUD_Y(16.0f) - (inv ? 2.0f : 0.0f), 6.0f, col);
    }
    {
        const float pe = ut - g_star_pulse_t;
        const float pulse = pe >= 0.0f && pe < 0.5f ? 1.1f - 0.1f * (pe / 0.5f) : 1.0f;   /* "pulse": 1.1 -> 1.0 over 0.5 s */
        for (int i = 0; i < 5; i++) {
            const float sx = 13.0f + (float)i * 15.0f, sy = HUD_Y(34.0f);
            const bool on = i < g_stars_active;
            const float re = ut - g_star_refill_t[i];
            if (re >= 0.0f && re < 0.3f) {   /* refill ripple */
                const float f = re / 0.3f, rr = 5.0f + 9.0f * f;
                C2D_DrawCircleSolid(sx, sy, Z_HUD, rr, C2D_Color32(0xE8, 0xB1, 0x48, (u8)(150.0f * (1.0f - f))));
                C2D_DrawCircleSolid(sx, sy, Z_HUD + 0.001f, rr * 0.75f, C2D_Color32(0x0A, 0x0A, 0x10, (u8)(150.0f * (1.0f - f))));
            }
            if (bb_theme_halloween()) {   /* game_star.gd set_star_theme: the 'halloween' animation, candy corn outline / filled */
                bb_art_hw_cell(on ? 2 : 1, sx, sy + 1.0f, 15.0f * pulse, 0.0f, C2D_Color32(0, 0, 0, 0x90), Z_HUD + 0.005f);
                bb_art_hw_cell(on ? 2 : 1, sx, sy, 15.0f * pulse, 0.0f, 0xFFFFFFFFu, Z_HUD + 0.01f);
            } else {
                bb_art_star(!on, sx, sy + 1.0f, 14.0f * pulse, C2D_Color32(0, 0, 0, 0x90), Z_HUD + 0.005f);   /* DropShadow */
                bb_art_star(!on, sx, sy, 14.0f * pulse, 0xFFFFFFFFu, Z_HUD + 0.01f);
            }
        }
    }
    /* ScoreDisplay: Anton, grey drop shadow, a 1.1 -> 1.0 pulse on every hit; the power-up multiplier under it */
    {
        const float pe = ut - g_score_pulse_t;
        const float sc = pe >= 0.0f && pe < 0.5f ? 1.1f - 0.1f * (pe / 0.5f) : 1.0f;
        snprintf(buf, sizeof(buf), "%06ld", (long)core->stats.score);
        const float cx = BOT_W - 7.0f - bb_font_width(BB_FACE_HEAD, 20, buf) * 0.5f, cy = inv ? BB_VIEW_H - 17.0f : 15.0f;
        bb_font_draw_xf(BB_FACE_HEAD, 20, cx, cy + 1.5f, sc, 0.0f, C2D_Color32(0x38, 0x38, 0x38, 0xFF), buf, Z_HUD + 0.02f);
        bb_font_draw_xf(BB_FACE_HEAD, 20, cx, cy, sc, 0.0f, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), buf, Z_HUD + 0.03f);
        snprintf(buf, sizeof(buf), "x%.1f", (double)core->score_mult_x10 / 10.0);
        bb_font_draw(BB_FACE_BOLD, 12, BOT_W - 7.0f, inv ? BB_VIEW_H - 48.0f : 27.0f, C2D_Color32(0xFF, 0x90, 0x00, 0xFF), C2D_AlignRight, buf, Z_HUD + 0.03f);
    }
#undef HUD_Y
}

void bb_view_draw_bottom_blank(void) {
    C2D_DrawRectSolid(0, 0, Z_BG, BOT_W, BB_VIEW_H, C2D_Color32(0x00, 0x00, 0x00, 0xFF));
}

void bb_view_text(C2D_TextBuf tb, float x, float y, float scale, u32 color, u32 align, const char* s, float z) {
    g_text_z = z;
    text(tb, x, y, scale, color, align, s);
}

void bb_view_draw_checker(u8 dim) {
    draw_checker((int64_t)(bb_ui_time() * 300000.0f));
    if (dim) C2D_DrawRectSolid(0, 0, Z_LANE, BB_VIEW_W, BB_VIEW_H, C2D_Color32(0, 0, 0, dim));
}

void bb_view_draw_bg(void) {
    draw_checker((int64_t)(bb_ui_time() * 300000.0f));   /* the checker drifts like the original menu backdrop */
    C2D_DrawRectSolid(0, 0, Z_LANE, BB_VIEW_W, BB_VIEW_H, C2D_Color32(0, 0, 0, 0x90));
}

/* game.tscn SeasonalOverlay/Halloween: two 'Spider Web 8' at (312, 144) and (1616, 144), scale 0.8 (the right one mirrored),
 * modulate alpha 0.335, over the scene (1080p -> the top screen at K, 13 px of each side cropped) */
static void halloween_webs(void) {
    if (!bb_theme_halloween()) return;
    const float k = 240.0f / 1080.0f, w = 800.0f * 0.8f * k, h = 381.0f * 0.8f * k;
    const u32 tint = C2D_Color32(0xFF, 0xFF, 0xFF, (u8)(0.335f * 255.0f));
    bb_art_hw_web(312.0f * k - 13.3f, 144.0f * k, w, h, tint, Z_FX + 0.07f);
    bb_art_hw_web(1616.0f * k - 13.3f, 144.0f * k, -w, h, tint, Z_FX + 0.07f);
}

/* ---------------- top screen: character scene (unobstructed) ---------------- */
void bb_view_draw_top(bb_view_t* v, const bb_rhythm_core_t* core) {
    int64_t now = core->song_cur_time_us;
    for (int vi = 0; vi < v->video_count; vi++) {
        if (bb_video_visible(&v->video[vi], now)) {
            bb_video_draw(&v->video[vi], now, Z_FX + 0.05f); /* video replaces the scene (final one is held on its last frame) */
            halloween_webs();
            v->frames_drawn++;
            return;
        }
    }
    draw_checker(now);
    draw_character(v, now);
    halloween_webs();

    /* Shutters: two bars close to the middle and open again over 0.3 s */
    float sel = (float)(now - v->shutter_t0_us) / 1000000.0f;
    if (sel >= 0.0f && sel < 0.3f) {
        float closed = sel < 0.15f ? sel / 0.15f : (0.3f - sel) / 0.15f;
        float bar = BB_VIEW_H * 0.5f * closed;
        C2D_DrawRectSolid(0, 0, Z_FX, BB_VIEW_W, bar, C2D_Color32(0, 0, 0, 0xFF));
        C2D_DrawRectSolid(0, BB_VIEW_H - bar, Z_FX, BB_VIEW_W, bar, C2D_Color32(0, 0, 0, 0xFF));
    }
    v->frames_drawn++;
}

/* ---------------- top-screen menus (plain, data-driven) ---------------- */
void bb_view_draw_menu(C2D_TextBuf tb, const char* title, const char* subtitle, const char* const* labels, const char* const* values,
                       const u32* colors, int count, int cursor, const char* footer) {
    g_text_z = Z_HUD;
    draw_checker(0);
    C2D_DrawRectSolid(0, 0, Z_LANE, BB_VIEW_W, BB_VIEW_H, C2D_Color32(0, 0, 0, 0x90));
    text(tb, 200.0f, 8.0f, 0.8f, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), C2D_AlignCenter, title);
    if (subtitle && subtitle[0]) {
        text(tb, 200.0f, 34.0f, 0.45f, C2D_Color32(0xFF, 0xCC, 0x40, 0xFF), C2D_AlignCenter, subtitle);
    }
    const int rows = 8;
    int first = cursor - rows / 2;
    if (first > count - rows) first = count - rows;
    if (first < 0) first = 0;
    for (int r = 0; r < rows && first + r < count; r++) {
        int i = first + r;
        float y = 54.0f + (float)r * 19.0f;
        u32 fg = colors ? colors[i] : C2D_Color32(0xE8, 0xE8, 0xF0, 0xFF);
        if (i == cursor) {
            C2D_DrawRectSolid(24.0f, y - 2.0f, Z_LANE + 0.01f, BB_VIEW_W - 48.0f, 18.0f, C2D_Color32(0xFF, 0x4A, 0x8A, 0xB0));
        }
        text(tb, 34.0f, y, 0.5f, fg, C2D_AlignLeft, labels[i]);
        if (values && values[i] && values[i][0]) {
            text(tb, BB_VIEW_W - 34.0f, y, 0.5f, fg, C2D_AlignRight, values[i]);
        }
    }
    if (footer && footer[0]) {
        text(tb, 200.0f, 218.0f, 0.38f, C2D_Color32(0xC8, 0xC8, 0xD8, 0xFF), C2D_AlignCenter, footer);
    }
}

void bb_view_draw_gameover_art(float x, float y, float w, float h, float src_x, float alpha, float z) {
    if (!g_gameover_sheet) {   /* no artwork: black, like the game */
        C2D_DrawRectSolid(x, y, z, w, h, C2D_Color32(0, 0, 0, (u8)(255.0f * alpha)));
        return;
    }
    C2D_Image img = g_img_gameover;
    Tex3DS_SubTexture st;
    set_sub(&st, src_x, 0, w, 240, 512, 256);
    img.subtex = &st;
    C2D_DrawParams dp;
    dp.pos.x = x; dp.pos.y = y; dp.pos.w = w; dp.pos.h = h;
    dp.center.x = 0.0f; dp.center.y = 0.0f; dp.depth = z; dp.angle = 0.0f;
    C2D_ImageTint tint;
    C2D_AlphaImageTint(&tint, alpha);
    C2D_DrawImage(img, &dp, &tint);
}

/* ---------------- scene viewer (Extras > Scene Viewer; sceneview_ui.c) ---------------- */
void bb_view_sv_prepare(bb_anim_t* anim, int sheet) {
    if (!anim || sheet < 0 || (uint32_t)sheet >= anim->sheet_count) return;
    const bb_anim_sheet_t* sh = &anim->sheet[sheet];
    if (!anim->keep_all) {   /* rolling-window stages: only this group stays resident */
        for (uint32_t i = 0; i < anim->sheet_count; i++) {
            bool keep = (int)i == sheet;
            for (int k = 0; k < sh->var_count; k++) if ((int)i == sh->var_idx[k]) keep = true;
            if (!keep && anim->sheet[i].resident) bb_anim_unload(anim, (int)i);
        }
    }
    bb_anim_load(anim, sheet);
    for (int k = 0; k < sh->var_count; k++) bb_anim_load(anim, sh->var_idx[k]);
}

void bb_view_sv_draw(const bb_anim_t* anim, int sheet, uint32_t frame, float scale, int64_t clock_us) {
    draw_checker(clock_us);
    if (!anim || sheet < 0 || (uint32_t)sheet >= anim->sheet_count) return;
    const uint32_t n = anim->sheet[sheet].frame_count;
    if (n == 0) return;
    const C2D_Image* img = bb_anim_frame(anim, sheet, frame < n ? frame : n - 1);
    if (!img) return;
    const float w = 1280.0f * CHAR_SCALE * K * scale, h = 720.0f * CHAR_SCALE * K * scale;
    C2D_DrawParams p;
    p.pos.x = 200.0f - w * 0.5f;
    p.pos.y = 120.0f - h * 0.5f;
    p.pos.w = w;
    p.pos.h = h;
    p.center.x = 0.0f;
    p.center.y = 0.0f;
    p.depth = Z_CHAR;
    p.angle = 0.0f;
    /* inset the frame's cell by one texel: drawn smaller than in play, the linear filter would pick up the neighbouring
     * cell's edge as a dark line along the left side */
    C2D_Image im = *img;
    Tex3DS_SubTexture st = *img->subtex;
    const float du = 1.0f / (float)img->tex->width, dv = 1.0f / (float)img->tex->height;
    st.left += du; st.right -= du; st.top -= dv; st.bottom += dv;
    im.subtex = &st;
    C2D_DrawImage(im, &p, NULL);
}
