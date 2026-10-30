/**
 * @file video.h
 * @brief Pre-rendered video player (BBVD, see tools/convert_video.py and MOD2DS).
 *
 * Frames are ETC1 cells in 1024x1024 pages; the playhead page is resident and the next one is paged in
 * on a worker thread. The soundtrack streams from its raw PCM file through a small ring of wave buffers on
 * one of BB_VIDEO_CHANNELS shared NDSP channels. Playback position is derived from the gameplay chart
 * time, so it stays locked to the music clock.
 *
 * A stage may have up to BB_VIDEO_MAX videos (full-motion mods chain a dozen clips). A video holds pages
 * and audio only inside its window: from BB_VIDEO_PREROLL_US before its start (pages prefetched, file
 * opened at start) until its picture and soundtrack are both over, then everything is released.
 */
#ifndef BB_VIDEO_H
#define BB_VIDEO_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <3ds.h>
#include <citro2d.h>

#define BB_VIDEO_MAX_PAGES  384   /* 8 frames per page: 3072 frames, 102 s at 30 fps per video */
#define BB_VIDEO_CH           6   /* first NDSP channel */
#define BB_VIDEO_CHANNELS     3   /* video i uses BB_VIDEO_CH + i % BB_VIDEO_CHANNELS */
#define BB_VIDEO_MAX         16
#define BB_VIDEO_PRELOAD      8   /* pages loaded synchronously on reset for a video that starts the run */
#define BB_VIDEO_PREROLL_US 2000000LL
/* pages prefetched ahead of the playhead (0.5 MiB each): bb_hw()->video_lookahead, hw_profile.c */
#define BB_VIDEO_ABUFS        4   /* soundtrack ring: 4 x 0.25 s */
#define BB_VIDEO_ABUF_FRAMES 8000

typedef struct {
    bool     loaded;
    bool     ndsp;
    int64_t  start_us;
    int64_t  hide_us;           /* picture hidden from here on (next loop keyframe); INT64_MAX for a final video */
    bool     outro;             /* asset.cfg final_video / final_audio (MOD2DS): start_us INT64_MIN in the file; each run starts
                                 * it 0.3 s after the chart's last note (game_outro_video.gd) via bb_video_set_outro_start */
    bool     no_picture;        /* hide_us INT64_MIN in the file: a final_audio without a final_video (no pages)
                                 */
    int      channel;
    float    fps;
    uint32_t frames, page_count, audio_frames;
    uint16_t frame_w, frame_h, cell_w, cell_h, cols, rows;
    char     dir[96];
    char     audio_name[32];
    char     page_name[BB_VIDEO_MAX_PAGES][32];

    C2D_SpriteSheet   page[BB_VIDEO_MAX_PAGES];   /* NULL when not resident */
    Tex3DS_SubTexture subtex;
    C2D_Image         image;

    /* worker thread (one page at a time) */
    Thread            thread;
    volatile int      pending;                    /* page index being loaded, -1 if none */
    volatile bool     done;
    uint32_t          job_page;

    /* streamed soundtrack */
    FILE*             af;
    int16_t*          abuf;                       /* linear memory, BB_VIDEO_ABUFS x BB_VIDEO_ABUF_FRAMES stereo frames */
    ndspWaveBuf       wb[BB_VIDEO_ABUFS];
    uint32_t          audio_read;                 /* frames read from the file so far */
    bool              audio_started, audio_eof;
    uint32_t          sync_loads, async_loads;
    uint32_t          load_ms_max, load_ms_total, read_ms_max;   /* page load / SD read timings */
    bool              reported;                       /* stats logged for this run */
} bb_video_t;

/* manifest_path: .../video/<id>.bbvid ; pages and PCM live next to it. Reads the manifest only. */
bool bb_video_init(bb_video_t* v, const char* manifest_path, bool ndsp_available, int channel);
/* An outro video (v->outro): the run's start time, the last note + 0.3 s on this chart. Call before bb_video_reset. */
void bb_video_set_outro_start(bb_video_t* v, int64_t start_us);
void bb_video_shutdown(bb_video_t* v);
void bb_video_reset(bb_video_t* v);                       /* new run: release everything, preload a video that starts the run */
/* Per-frame: page residency, soundtrack start / refill, and release once the video is over. */
void bb_video_update(bb_video_t* v, int64_t chart_time_us);
bool bb_video_visible(const bb_video_t* v, int64_t chart_time_us);   /* start <= t < hide */
bool bb_video_finished(const bb_video_t* v, int64_t chart_time_us);  /* t >= start + duration */
void bb_video_set_paused(bb_video_t* v, bool paused);
void bb_video_draw(bb_video_t* v, int64_t chart_time_us, float z);

#endif
