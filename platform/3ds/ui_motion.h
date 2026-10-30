/**
 * @file ui_motion.h
 * @brief Small animation helpers shared by every menu: a global UI clock, a per-screen "entered" timer (reset by
 *        bb_ui_enter on every screen change), easing, staggered appear curves and exponential smoothing for cursors.
 */
#ifndef BB_UI_MOTION_H
#define BB_UI_MOTION_H

#include <3ds.h>

void  bb_ui_tick(float dt_s);                 /* once per frame, before drawing */
void  bb_ui_enter(void);                      /* a new screen / sub-screen starts: restarts every appear() curve */
float bb_ui_time(void);                       /* seconds since start (free running) */
float bb_ui_enter_time(void);                 /* seconds since the last bb_ui_enter */
float bb_ui_dt(void);

float bb_ui_ease_out(float t);                /* cubic, clamps t to 0..1 */
float bb_ui_ease_out_back(float t);           /* slight overshoot, used for pops */
/* 0..1 curve starting `delay` seconds after the screen was entered and lasting `dur` seconds, eased out. */
float bb_ui_appear(float delay, float dur);
void  bb_ui_set_reduce_motion(bool on);   /* Settings > Gameplay > Reduce Motion: menu slide-ins finish instantly, no shakes / sparkles / drifting particles */
bool  bb_ui_reduce_motion(void);
/* Menu beat clock: main sets the scene music position in beats (-1 = no music); pulse is 1 on the beat and decays (EventManager.menu_beat look). */
void  bb_ui_set_beat(float beats);
float bb_ui_beat_pulse(void);
/* Seconds since the last menu beat at `bpm` (99 = no beat clock, or Reduce Motion): drives BeatSyncComponent-style tweens. */
float bb_ui_beat_since(float bpm);
/* Touch zones (bottom screen): the draw code registers rectangles with an id every frame (cleared before drawing); input hit-tests
 * them on the next frame. bb_ui_zone_hit returns the topmost id or -1. */
void  bb_ui_zone_clear(void);
void  bb_ui_zone_add(int id, float x, float y, float w, float h);
int   bb_ui_zone_hit(int px, int py);
bool  bb_ui_zone_rect(int id, float* x, float* w);   /* left edge and width of the zone registered last frame */
/* Exponential approach of *cur to target (rate ~ 12..20 = snappy); returns the new value. */
float bb_ui_smooth(float* cur, float target, float rate);
/* Black fade-in over the first 0.28 s of a screen (drawn on top, z 0.95). */
void  bb_ui_draw_fade(void);

#endif
