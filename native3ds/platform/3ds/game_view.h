/**
 * @file game_view.h
 * @brief Top-screen (400x240) gameplay presentation: character animation, camera,
 *        shutters, note highway, HUD, results/fail overlays.
 *
 * Presentation state is driven only by the rhythm core's events and chart time
 * (core->song_cur_time_us), so animation, notes and judgments share one timeline.
 * The bottom screen is not touched here.
 */
#ifndef BB_GAME_VIEW_H
#define BB_GAME_VIEW_H

#include <3ds.h>
#include <citro2d.h>
#include "rhythm_core.h"
#include "save_data.h"
#include "anim.h"
#include "video.h"

#define BB_VIEW_W 400.0f
#define BB_VIEW_H 240.0f

typedef struct {
    bb_anim_t*  anim;
    bb_video_t* video;          /* video array (BB_VIDEO_MAX), may be NULL */
    int         video_count;

    /* Character */
    int         sheet_idx;          /* current loop's normal sheet, -1 if none */
    int         draw_sheet;         /* sheet actually drawn this cycle (a variation or sheet_idx) */
    uint32_t    rng;
    int         loaded_next_idx;    /* sheet preloaded for the next loop, -1 if none */
    int         pending_sheet;      /* rolling window: a loop began before its sheet streamed in; switched to once loaded, -1 if none */
    bool        tween_active;
    int64_t     tween_t0_us;
    int64_t     tween_dur_us;
    uint32_t    frame;
    int64_t     anim_duration_us;   /* GameManager animation_duration */
    float       gray;               /* 0..1 desaturation */
    int64_t     pulse_t0_us;
    int64_t     shake_t0_us;

    /* Camera (Character/Root tween, EXPO ease-out) */
    uint32_t    cam_next_key;
    float       cam_pos_x, cam_pos_y, cam_scale;
    float       cam_from_x, cam_from_y, cam_from_s;
    float       cam_to_x, cam_to_y, cam_to_s;
    int64_t     cam_t0_us;
    float       cam_dur_s;

    /* Effects / feedback */
    int64_t     shutter_t0_us;
    bool        shutter_started;
    int64_t     judge_t0_us;
    bb_judgment_t judge;
    const char* absent_text;
    uint32_t    absent_rot;
    float       life_pulse;

    /* Stats */
    uint32_t    frames_drawn;
    uint32_t    sheet_loads_failed;
} bb_view_t;

bool bb_view_ui_init(void);
void bb_view_ui_shutdown(void);
void bb_view_set_gameover(const char* t3x_path);
void bb_view_init(bb_view_t* v, bb_anim_t* anim);
/* Reset for a (re)started stage; loads the first sheet(s). */
void bb_view_reset(bb_view_t* v, const bb_rhythm_core_t* core);
/* Apply one core event (hit/miss/loop/shutter...). Call for every polled event, then bb_view_tick once per frame. */
void bb_view_event(bb_view_t* v, const bb_rhythm_core_t* core, const bb_event_t* ev);
void bb_view_tick(bb_view_t* v, const bb_rhythm_core_t* core);

/* ---- top screen (400x240) ---- */
void bb_view_draw_top(bb_view_t* v, const bb_rhythm_core_t* core);            /* character, camera, shutters, video; nothing else */
void bb_view_draw_menu(C2D_TextBuf tb, const char* title, const char* subtitle, const char* const* labels, const char* const* values,
                       const u32* colors, int count, int cursor, const char* footer);
/* The stage's game-over art (400x240): the (src_x, 0, w, 240) part drawn into (x, y, w, h) at `alpha` (black if the stage has none). */
void bb_view_draw_gameover_art(float x, float y, float w, float h, float src_x, float alpha, float z);
/* The Blindfold power-up: bottom-screen notes fade out near the hit line. */
void bb_view_set_blindfold(bool on);
void bb_view_set_modifiers(bool no_hud, bool no_beat_bar);

/* ---- bottom screen (320x240): four-lane highway, judgments and rhythm HUD ---- */
void bb_view_draw_bottom(bb_view_t* v, const bb_rhythm_core_t* core, C2D_TextBuf tb, int ctrl_mode, float scroll_mult);
void bb_view_draw_bottom_blank(void);
/* Lanes currently held (bit = input type), for the receptor glow; main.c sets it every frame from the same lane mask the core gets. */
void bb_view_set_held_lanes(uint8_t mask);
/* Lanes physically pressed this frame (buttons, D-Pad or touch): the receptor bounce (DownscrollLaneArrow.pulse). */
void bb_view_lanes_pressed(uint8_t mask);
/* Scroll Mode (bb_scroll_mode_t, save_data.h) and the lane a bottom-screen touch presses in it (-1: none). */
void bb_view_set_scroll_mode(int mode);
/* Book layout for Legacy mods (held turned a quarter left): needs the side scroll mode; u0..u1 = the art's span of a frame cell. */
void bb_view_set_book(bool on, float u0, float u1);
int  bb_view_touch_lane(int px, int py);
/* Lane colours and the highway's left-to-right order (DOWNSCROLL_LANE_ORDER), shared with the bottom-screen companion. */
u32  bb_view_lane_color(int input_type, u8 alpha);
int  bb_view_lane_at_slot(int slot);
/* Free text helper (used by the optional diagnostics overlay). */
void bb_view_text(C2D_TextBuf tb, float x, float y, float scale, u32 color, u32 align, const char* s, float z);
/* Checker background + dim, for full-screen top-screen panels (screens.c). */
void bb_view_draw_bg(void);
/* The same drifting checker at a chosen dim (0 = the in-game brightness); the main menu's top screen. */
void bb_view_draw_checker(u8 dim);
/* While true every text call uses the system font (set around all bottom-screen drawing; the bottom screen is not restyled). */
void bb_view_set_system_text(bool on);

/* Scene viewer (Extras > Scene Viewer, sceneview_ui.c). Separate from the gameplay drawing: the loaded stage's sheet `sheet` (a
 * variation sheet is fine) at frame `frame` (clamped) over the stage's scrolling checker, camera at rest (gameplay size at scale 1). */
void bb_view_sv_draw(const bb_anim_t* anim, int sheet, uint32_t frame, float scale, int64_t clock_us);
/* Make `sheet` and its variations resident (synchronous); stages whose sheets do not all fit free every other sheet first. */
void bb_view_sv_prepare(bb_anim_t* anim, int sheet);

#endif /* BB_GAME_VIEW_H */
