/**
 * @file music_ui.h
 * @brief Extras > Music: the original Sound Test (sound_test.tscn) on both screens.
 *
 * TOP: the studio monitor (ui_studio_* layers, the speaker cones pulse on the beat) showing NOW PLAYING, the title, the BPM and the
 * progress bar. BOTTOM: the record carousel (Record.tscn: vinyl backing, the record's coloured centre, outline on the selected one which
 * spins), locked records dark with a lock, < > arrows and back. A record plays when it is owned (shop Music items 201-207, 297-299);
 * owning all of them unlocks Cathy's Headphones (sound_test.check_unlocks). Assets: tools/build_viewers.py.
 * main.c owns the stream: it plays bb_music_want() and reports the position with bb_music_set_pos().
 */
#ifndef BB_MUSIC_UI_H
#define BB_MUSIC_UI_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>
#include "save_data.h"

void bb_music_open(bb_save_t* save);
void bb_music_close(void);
/* 0 while open, 1 when left (ui_back played). tap / tx / ty: this frame's bottom-screen tap (zones from the last bottom draw). */
int  bb_music_update(u32 kDown, u32 kRep, bool tap, int tx, int ty);
/* Full romfs path of the record to stream ("romfs:/music/st_<k>.wav"), or NULL for silence (the sound test stops all music). */
const char* bb_music_want(void);
/* The stream's position and length (us) this frame; drives the progress bar and the beat pulses. */
void bb_music_set_pos(int64_t pos_us, int64_t dur_us);
/* The save changed (the Headphones collectable was granted): persist it. Cleared by the call. */
bool bb_music_take_dirty(void);
void bb_music_draw_top(void);
void bb_music_draw_bottom(void);

#endif
