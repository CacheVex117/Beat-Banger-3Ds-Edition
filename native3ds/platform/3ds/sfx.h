/**
 * @file sfx.h
 * @brief Stage sound effects / voice banks on dedicated NDSP channels (BBSF v1 / v2 bank).
 *
 * v1: fixed records (a sound_loop holds 8 clip slots, a voice_bank 12). v2 (MOD2DS, only when a level needs more): the same
 * header and clip / one-shot tables; a loop record is ts, mode, n, then n clip ids, a voice record ts, n, then n ids, each
 * padded to 4 bytes.
 *
 * Mirrors the reference: a loop-sound bank cycles through its clips on every non-ghost hit
 * (game_loopsound.gd), voice banks cycle on trigger_voice notes (game_voice.gd) and one-shots
 * fire at their keyframe time (game_oneshotsound.gd). Each of the three players holds one
 * sound at a time, like the single AudioStreamPlayer nodes in the game.
 */
#ifndef BB_SFX_H
#define BB_SFX_H

#include <stdint.h>
#include <stdbool.h>
#include <3ds.h>

#define BB_SFX_MAX_CLIPS   512   /* SD-card mods (Beat Banger Encore: 252 clips) */
#define BB_SFX_LOOP_CLIPS   32   /* clips per sound_loop record (BBSF v1 stores 8) */
#define BB_SFX_VOICE_CLIPS  48   /* clips per voice_bank record (BBSF v1 stores 12; Encore: 30) */
#define BB_SFX_MAX_LOOPS    64   /* SD-card mods: sound_loop keyframes (Toriel wants More: 32) */
#define BB_SFX_MAX_ONESHOTS 512   /* SD-card mods (Arte's Rush: 310 timed one-shots) */
#define BB_SFX_MAX_VOICES   64   /* SD-card mods: voice_bank keyframes (Femdog: 20) */

#define BB_SFX_CH_LOOP   2
#define BB_SFX_CH_VOICE  3
#define BB_SFX_CH_ONESHOT 4
#define BB_SFX_CH_UI      5   /* miss / damage / glass (named clips) */

typedef struct {
    char     name[32];
    uint32_t byte_offset;
    uint32_t frames;
} bb_sfx_clip_t;

typedef struct { int64_t ts_us; uint32_t mode, n; uint16_t clip[BB_SFX_LOOP_CLIPS]; } bb_sfx_loop_t;
typedef struct { int64_t ts_us; uint32_t clip; } bb_sfx_oneshot_t;
typedef struct { int64_t ts_us; uint32_t n; uint16_t clip[BB_SFX_VOICE_CLIPS]; } bb_sfx_voice_t;

typedef struct {
    bool     loaded;
    bool     ndsp;
    uint32_t sample_rate;
    uint32_t clip_count, loop_count, oneshot_count, voice_count;
    bb_sfx_clip_t    clips[BB_SFX_MAX_CLIPS];
    bb_sfx_loop_t    loops[BB_SFX_MAX_LOOPS];
    bb_sfx_oneshot_t oneshots[BB_SFX_MAX_ONESHOTS];
    bb_sfx_voice_t   voices[BB_SFX_MAX_VOICES];
    int16_t*         pcm;           /* linear memory */
    uint32_t         pcm_bytes;

    ndspWaveBuf wb[4];
    int      active_loop_bank, loop_cursor;
    int      active_voice_bank, voice_cursor;
    uint32_t next_oneshot;
    uint32_t plays;
} bb_sfx_t;

bool bb_sfx_init(bb_sfx_t* s, const char* path, bool ndsp_available);
void bb_sfx_shutdown(bb_sfx_t* s);
void bb_sfx_reset(bb_sfx_t* s);                 /* new run: rewind banks and one-shot cursor */
void bb_sfx_stop(bb_sfx_t* s);
/* Play a named shared clip ("__miss", "__damage", "__glass1".."__glass3") on the UI channel; false when the bank has none. */
bool bb_sfx_play_named(bb_sfx_t* s, const char* name);
/* A non-ignored hit on a note with this chart timestamp. */
void bb_sfx_on_hit(bb_sfx_t* s, int64_t note_ts_us, bool trigger_voice);
/* Fire due one-shots; call every frame with the chart time. */
void bb_sfx_tick(bb_sfx_t* s, int64_t chart_time_us);

#endif
