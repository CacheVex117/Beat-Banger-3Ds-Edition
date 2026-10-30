/**
 * @file sceneview_ui.h
 * @brief Extras > Scene Viewer: the original scene_viewer.tscn on both screens.
 *
 * TOP: the selected level's character playing one loop per beat over its checker (the sheet's frames across one beat, a weighted
 * variation per loop, a 1.1 -> 1.0 pump on every change), or one of its climax videos; X toggles movie mode (1.5x zoom).
 * BOTTOM: the levels (thumbnails, locked until beaten), the animation selector (numbered buttons in timeline order, videos in gold),
 * the song selector (No Song / the level songs of beaten levels) and the custom BPM clock (120 by default, 30-300, play / pause).
 *
 * main.c owns loading: when bb_sv_want_stage() returns an id it loads that stage (anim, videos, chart) and calls bb_sv_stage_loaded();
 * the viewer draws "Loading" for one frame first. Song streaming: main plays bb_sv_want_music() and reports the position.
 */
#ifndef BB_SCENEVIEW_UI_H
#define BB_SCENEVIEW_UI_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>
#include "save_data.h"
#include "rhythm_core.h"
#include "anim.h"
#include "video.h"

void bb_sv_open(bb_save_t* save);
void bb_sv_close(void);                      /* stops any video soundtrack it started */
/* A stage id to load now (the loading frame has been shown), else NULL. */
const char* bb_sv_want_stage(void);
/* The requested stage is loaded: its chart (loop keyframes), anim sheets and climax videos (their pages stream as usual). */
void bb_sv_stage_loaded(const bb_chart_t* chart, bb_anim_t* anim, bb_video_t* videos, int video_count);
/* 0 while open, 1 when left (ui_back played). */
/* Touch, every frame before bb_sv_update: a drag scrolls the bottom screen, a release without a drag taps there. */
void bb_sv_touch(bool held, bool tap, int x, int y);
int  bb_sv_update(u32 kDown, u32 kRep, bool tap, int tx, int ty);
/* Full romfs path of the song to stream, or NULL (No Song: the viewer runs its own BPM clock). */
const char* bb_sv_want_music(void);
void bb_sv_set_music_pos(int64_t pos_us);
void bb_sv_draw_top(void);
void bb_sv_draw_bottom(void);

#endif
