/**
 * @file video.c
 * @brief Pre-rendered video player (see video.h).
 */
#include "video.h"
#include "diag.h"
#include "hw_profile.h"
#include "t3x_io.h"
#include <stdlib.h>
#include <string.h>

#define AUDIO_RATE 32000

/* which video currently plays on each shared channel: only the owner may clear or pause it */
static bb_video_t* g_chan_owner[BB_VIDEO_CHANNELS];
/* page loads in flight across all videos: one at a time. On hardware two overlapping clips (the next one's pre-roll
 * while the current one streams) loading at once took the game from ~19 to 33 ms frames (Tracer, console telemetry). */
static int g_loads_inflight;

static uint16_t rd_u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int chan_slot(const bb_video_t* v) {
    int s = v->channel - BB_VIDEO_CH;
    return (s >= 0 && s < BB_VIDEO_CHANNELS) ? s : -1;
}

static int64_t duration_us(const bb_video_t* v) {
    int64_t pic = (int64_t)((double)v->frames * 1000000.0 / (double)v->fps);
    int64_t snd = (int64_t)v->audio_frames * 1000000 / AUDIO_RATE;
    return pic > snd ? pic : snd;
}

bool bb_video_init(bb_video_t* v, const char* manifest_path, bool ndsp_available, int channel) {
    memset(v, 0, sizeof(*v));
    v->pending = -1;
    v->channel = channel;
    v->ndsp = ndsp_available;

    FILE* fp = fopen(manifest_path, "rb");
    if (!fp) {
        return false;
    }
    uint8_t head[84];
    bool ok = fread(head, 1, sizeof(head), fp) == sizeof(head) && memcmp(head, "BBVD", 4) == 0 && rd_u32(head + 4) == 2;
    if (ok) {
        v->start_us = (int64_t)((uint64_t)rd_u32(head + 8) | ((uint64_t)rd_u32(head + 12) << 32));
        v->hide_us = (int64_t)((uint64_t)rd_u32(head + 16) | ((uint64_t)rd_u32(head + 20) << 32));
        uint32_t fb = rd_u32(head + 24);
        memcpy(&v->fps, &fb, sizeof(float));
        v->frames = rd_u32(head + 28);
        v->frame_w = rd_u16(head + 32);
        v->frame_h = rd_u16(head + 34);
        v->cell_w = rd_u16(head + 36);
        v->cell_h = rd_u16(head + 38);
        v->cols = rd_u16(head + 40);
        v->rows = rd_u16(head + 42);
        v->page_count = rd_u32(head + 44);
        v->audio_frames = rd_u32(head + 48);
        memcpy(v->audio_name, head + 52, 31);
        v->audio_name[31] = '\0';
        v->outro = v->start_us == INT64_MIN;
        v->no_picture = v->outro && v->hide_us == INT64_MIN;
        if (v->outro) v->start_us = v->hide_us = INT64_MAX / 4;   /* nothing plays until a run sets the start (no overflow in start + duration) */
        ok = (v->page_count > 0 || v->no_picture) && v->page_count <= BB_VIDEO_MAX_PAGES && v->frames > 0 && v->fps > 0.0f &&
             v->cols > 0 && v->rows > 0;
        for (uint32_t i = 0; ok && i < v->page_count; i++) {
            ok = fread(v->page_name[i], 1, 32, fp) == 32;
            v->page_name[i][31] = '\0';
        }
        if (ok) {
            const char* slash = strrchr(manifest_path, '/');
            size_t dl = slash ? (size_t)(slash - manifest_path) : 0;
            if (dl >= sizeof(v->dir)) dl = sizeof(v->dir) - 1;
            memcpy(v->dir, manifest_path, dl);
            v->dir[dl] = '\0';
            if (v->ndsp && v->audio_frames > 0 && chan_slot(v) >= 0 && !g_chan_owner[chan_slot(v)]) {
                /* fresh channel state; apply_volumes() sets the mix afterwards */
                float mix[12];
                memset(mix, 0, sizeof(mix));
                mix[0] = 1.0f;
                mix[1] = 1.0f;
                ndspChnReset(v->channel);
                ndspChnInitParams(v->channel);
                ndspChnSetMix(v->channel, mix);
            }
        }
    }
    fclose(fp);
    v->loaded = ok;
    return ok;
}

void bb_video_set_outro_start(bb_video_t* v, int64_t start_us) {
    if (!v->loaded || !v->outro) {
        return;
    }
    v->start_us = start_us;
    v->hide_us = v->no_picture ? start_us : INT64_MAX;   /* the picture stays up until the game moves on */
}

static void join_thread(bb_video_t* v) {
    if (v->thread) {
        threadJoin(v->thread, U64_MAX);
        threadFree(v->thread);
        v->thread = NULL;
        if (g_loads_inflight > 0) g_loads_inflight--;
    }
    v->pending = -1;
    v->done = false;
}

/* Read the page file in 32 KiB pieces, then decode from memory (t3x_io.c: the song stream shares the SD card with it). */
static C2D_SpriteSheet load_page(bb_video_t* v, uint32_t p) {
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", v->dir, v->page_name[p]);
    uint64_t t0 = osGetTime();
    uint32_t read_ms = 0;
    C2D_SpriteSheet sh = bb_t3x_load(path, &read_ms);
    if (read_ms > v->read_ms_max) v->read_ms_max = read_ms;
    if (sh) {
        C3D_TexSetFilter(C2D_SpriteSheetGetImage(sh, 0).tex, GPU_LINEAR, GPU_LINEAR);
    }
    uint32_t ms = (uint32_t)(osGetTime() - t0);
    if (ms > v->load_ms_max) v->load_ms_max = ms;
    v->load_ms_total += ms;
    return sh;
}

static void worker(void* arg) {
    bb_video_t* v = (bb_video_t*)arg;   /* each video carries its own job, so two videos can prefetch at once */
    C2D_SpriteSheet sh = load_page(v, v->job_page);
    v->page[v->job_page] = sh;
    v->done = true;
}

static void request_async(bb_video_t* v, uint32_t p) {
    if (p >= v->page_count || v->page[p] || v->pending >= 0 || g_loads_inflight > 0) {
        return;
    }
    v->done = false;
    v->job_page = p;
    v->pending = (int)p;
    /* New 3DS / 2DS: load on the spare core 2 (like anim.c's sheet streamer). On core 0 a page load (SD read + LZ
     * decompress, ~250 KB per page every ~267 ms at 30 fps) competes with the game loop and arrives late on hardware;
     * old models have no core 2, so there it runs below the game thread (hw_profile.c). */
    v->thread = bb_hw_worker_thread(worker, v, 128 * 1024);
    if (!v->thread) {
        v->pending = -1;
        v->page[p] = load_page(v, p);
        v->sync_loads++;
    } else {
        g_loads_inflight++;
        v->async_loads++;
    }
}

static void free_pages(bb_video_t* v) {
    for (uint32_t i = 0; i < v->page_count; i++) {
        if (v->page[i]) {
            C2D_SpriteSheetFree(v->page[i]);
            v->page[i] = NULL;
        }
    }
}

static void audio_stop(bb_video_t* v) {
    int s = chan_slot(v);
    if (v->ndsp && s >= 0 && g_chan_owner[s] == v) {
        ndspChnWaveBufClear(v->channel);
        ndspChnSetPaused(v->channel, false);
        g_chan_owner[s] = NULL;
    }
    if (v->af) {
        fclose(v->af);
        v->af = NULL;
    }
    if (v->abuf) {
        linearFree(v->abuf);
        v->abuf = NULL;
    }
    v->audio_started = false;
    v->audio_eof = false;
    v->audio_read = 0;
}

/* Fill ring buffer k from the file and queue it; false at the end of the soundtrack. */
static bool audio_fill(bb_video_t* v, int k) {
    if (v->audio_eof || !v->af) {
        return false;
    }
    uint32_t left = v->audio_frames > v->audio_read ? v->audio_frames - v->audio_read : 0;
    uint32_t want = left < BB_VIDEO_ABUF_FRAMES ? left : BB_VIDEO_ABUF_FRAMES;
    int16_t* dst = v->abuf + (size_t)k * BB_VIDEO_ABUF_FRAMES * 2;
    size_t got = want ? fread(dst, 4, want, v->af) : 0;
    if (got == 0) {
        v->audio_eof = true;
        return false;
    }
    v->audio_read += (uint32_t)got;
    if (v->audio_read >= v->audio_frames) v->audio_eof = true;
    DSP_FlushDataCache(dst, got * 4);
    memset(&v->wb[k], 0, sizeof(v->wb[k]));
    v->wb[k].data_vaddr = dst;
    v->wb[k].nsamples = (u32)got;
    v->wb[k].looping = false;
    ndspChnWaveBufAdd(v->channel, &v->wb[k]);
    return true;
}

/* Start the soundtrack at rel_us into the video (late starts seek, so a resumed or skipped-into video stays in sync). */
static void audio_start(bb_video_t* v, int64_t rel_us) {
    v->audio_started = true;                          /* even on failure: try once per run */
    int s = chan_slot(v);
    if (!v->ndsp || s < 0 || v->audio_frames == 0) {
        return;
    }
    uint32_t skip = (uint32_t)(rel_us * AUDIO_RATE / 1000000);
    if (skip >= v->audio_frames) {
        return;
    }
    char apath[160];
    snprintf(apath, sizeof(apath), "%s/%s", v->dir, v->audio_name);
    v->af = fopen(apath, "rb");
    v->abuf = (int16_t*)linearAlloc((size_t)BB_VIDEO_ABUFS * BB_VIDEO_ABUF_FRAMES * 4);
    if (!v->af || !v->abuf || fseek(v->af, (long)skip * 4, SEEK_SET) != 0) {
        audio_stop(v);
        v->audio_started = true;
        return;
    }
    if (g_chan_owner[s] && g_chan_owner[s] != v) {
        audio_stop(g_chan_owner[s]);                  /* an older clip still on this channel: the new one wins */
    }
    g_chan_owner[s] = v;
    v->audio_read = skip;
    ndspChnWaveBufClear(v->channel);
    ndspChnSetInterp(v->channel, NDSP_INTERP_LINEAR);
    ndspChnSetRate(v->channel, (float)AUDIO_RATE);
    ndspChnSetFormat(v->channel, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetPaused(v->channel, false);
    for (int k = 0; k < BB_VIDEO_ABUFS; k++) {
        if (!audio_fill(v, k)) break;
    }
}

static void audio_pump(bb_video_t* v) {
    if (!v->af || chan_slot(v) < 0 || g_chan_owner[chan_slot(v)] != v) {
        return;
    }
    for (int k = 0; k < BB_VIDEO_ABUFS && !v->audio_eof; k++) {
        if (v->wb[k].status == NDSP_WBUF_DONE) {
            audio_fill(v, k);
        }
    }
}

static void release(bb_video_t* v) {
    join_thread(v);
    audio_stop(v);
    free_pages(v);
}

void bb_video_reset(bb_video_t* v) {
    if (!v->loaded) {
        return;
    }
    release(v);
    v->reported = false;
    v->sync_loads = v->async_loads = 0;
    v->load_ms_max = v->load_ms_total = v->read_ms_max = 0;
    /* A video that starts the run: preload up to 8 pages (4 MiB) synchronously so playback never competes with the song
     * stream for the SD card. Later videos are prefetched asynchronously during their pre-roll. */
    if (v->start_us < BB_VIDEO_PREROLL_US) {
        uint32_t pre = v->page_count < BB_VIDEO_PRELOAD ? v->page_count : BB_VIDEO_PRELOAD;
        for (uint32_t i = 0; i < pre; i++) {
            v->page[i] = load_page(v, i);
            v->sync_loads++;
        }
    }
}

void bb_video_update(bb_video_t* v, int64_t t) {
    if (!v->loaded) {
        return;
    }
    if (v->pending >= 0 && v->done) {
        join_thread(v);
    }
    const int64_t end = v->start_us + duration_us(v);
    if (t >= end + 250000) {                          /* over: give the pages, the file and the channel back */
        release(v);
        if (!v->reported && (v->sync_loads || v->async_loads)) {   /* once per run, while the stage still plays */
            v->reported = true;
            bb_diag_log("video %s done: sync=%lu async=%lu load_ms max=%lu avg=%lu read_ms max=%lu", v->page_name[0],
                        (unsigned long)v->sync_loads, (unsigned long)v->async_loads, (unsigned long)v->load_ms_max,
                        (unsigned long)(v->load_ms_total / (v->sync_loads + v->async_loads)), (unsigned long)v->read_ms_max);
        }
        return;
    }
    if (t < v->start_us - BB_VIDEO_PREROLL_US) {
        return;
    }

    int64_t rel = t - v->start_us;
    const bool picture = t < v->hide_us;
    if (picture) {
        uint32_t cur = 0;
        if (rel > 0) {
            uint32_t f = (uint32_t)((double)rel * (double)v->fps / 1000000.0);
            if (f >= v->frames) f = v->frames - 1;
            cur = f / ((uint32_t)v->cols * v->rows);
        }
        /* free pages behind the playhead (only when the video is longer than the preload window) */
        for (uint32_t i = 0; v->page_count > BB_VIDEO_PRELOAD && i < cur && i < v->page_count; i++) {
            if (v->page[i] && !(v->pending == (int)i)) {
                C2D_SpriteSheetFree(v->page[i]);
                v->page[i] = NULL;
            }
        }
        if (rel >= 0 && !v->page[cur] && v->pending != (int)cur) {
            int was = v->pending;
            join_thread(v);              /* needed now: a different page is loading; wait, then load this one */
            v->page[cur] = load_page(v, cur);
            v->sync_loads++;
            bb_diag_log("video %s: sync load page %lu at rel=%.3fs (pending was %d)", v->page_name[0], (unsigned long)cur,
                        (double)rel / 1e6, was);
        }
        if (v->pending < 0) {   /* keep the playhead page and the next video_lookahead pages resident (one load at a time) */
            for (uint32_t p = cur; p <= cur + bb_hw()->video_lookahead && p < v->page_count; p++) {
                if (!v->page[p]) {
                    request_async(v, p);
                    break;
                }
            }
        }
    } else if (v->pending < 0) {
        free_pages(v);                                        /* hidden: only the soundtrack plays out */
    }

    if (rel >= 0 && !v->audio_started) {
        audio_start(v, rel);
    } else if (v->audio_started) {
        audio_pump(v);
    }
}

bool bb_video_visible(const bb_video_t* v, int64_t t) {
    return v->loaded && t >= v->start_us && t < v->hide_us;
}

bool bb_video_finished(const bb_video_t* v, int64_t t) {
    if (!v->loaded) {
        return true;
    }
    return t >= v->start_us + (int64_t)((double)v->frames * 1000000.0 / (double)v->fps);
}

void bb_video_set_paused(bb_video_t* v, bool paused) {
    int s = chan_slot(v);
    if (v->loaded && v->ndsp && s >= 0 && g_chan_owner[s] == v) {
        ndspChnSetPaused(v->channel, paused);
    }
}

void bb_video_draw(bb_video_t* v, int64_t t, float z) {
    if (!v->loaded) {
        return;
    }
    int64_t rel = t - v->start_us;
    if (rel < 0 || t >= v->hide_us) {
        return;
    }
    uint32_t f = (uint32_t)((double)rel * (double)v->fps / 1000000.0);
    if (f >= v->frames) f = v->frames - 1;
    uint32_t per_page = (uint32_t)v->cols * v->rows;
    uint32_t p = f / per_page, cell = f % per_page;
    if (p >= v->page_count || !v->page[p]) {
        return;
    }
    C2D_Image base = C2D_SpriteSheetGetImage(v->page[p], 0);
    float tw = 1024.0f, th = 1024.0f;
    float x = (float)((cell % v->cols) * v->cell_w), y = (float)((cell / v->cols) * v->cell_h);
    v->subtex.width = v->frame_w;
    v->subtex.height = v->frame_h;
    v->subtex.left = x / tw;
    v->subtex.right = (x + (float)v->frame_w) / tw;
    v->subtex.top = 1.0f - y / th;
    v->subtex.bottom = 1.0f - (y + (float)v->frame_h) / th;
    v->image.tex = base.tex;
    v->image.subtex = &v->subtex;

    C2D_DrawParams dp;
    dp.pos.x = 0.0f;
    dp.pos.y = 0.0f;
    dp.pos.w = 400.0f;
    dp.pos.h = 240.0f;
    dp.center.x = 0.0f;
    dp.center.y = 0.0f;
    dp.depth = z;
    dp.angle = 0.0f;
    C2D_DrawImage(v->image, &dp, NULL);
}

void bb_video_shutdown(bb_video_t* v) {
    if (!v->loaded && !v->abuf && !v->af) {
        return;
    }
    if (v->loaded) {   /* sync loads during play are stalls; pre-roll and the reset preload are expected */
        bb_diag_log("video %s: pages=%lu sync_loads=%lu async_loads=%lu audio_frames=%lu", v->page_name[0],
                    (unsigned long)v->page_count, (unsigned long)v->sync_loads, (unsigned long)v->async_loads,
                    (unsigned long)v->audio_frames);
    }
    release(v);
    v->loaded = false;
}
