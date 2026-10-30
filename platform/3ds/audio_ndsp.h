/**
 * @file audio_ndsp.h
 * @brief NDSP streaming audio subsystem & monotonic gameplay clock for 3DS.
 *
 * Manages a 4-buffer NDSP ring in linear memory, streaming either a RIFF
 * 16-bit stereo PCM WAV file (romfs:/audio/cathy_e1.wav) or a procedural
 * 126 BPM rhythm track (for the synthetic calibration chart).
 *
 * Sequence-aware sample accounting:
 *   Each queued buffer is assigned a sequence ID. When querying the hardware
 *   playback offset via ndspChnGetSamplePos(0), we also query
 *   ndspChnGetWaveBufSeq(0) to atomically correlate in-buffer position with
 *   cumulative completed samples, preventing boundary race conditions.
 *
 * Safe SFX ownership:
 *   Double-buffered SFX slots (2 x 512 frames) with proper wavebuf retirement
 *   to guarantee the DSP never reads memory being actively overwritten.
 */
#ifndef BB_AUDIO_NDSP_H
#define BB_AUDIO_NDSP_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __3DS__
#include <3ds.h>
#else
/* Minimal NDSP types for host unit tests & mocked DSP simulation */
typedef struct tag_ndspWaveBuf ndspWaveBuf;
struct tag_ndspWaveBuf {
    const void* data_vaddr;
    uint32_t nsamples;
    void* adpcm_data;
    uint32_t offset;
    bool looping;
    uint8_t status;
    uint16_t sequence_id;
    ndspWaveBuf* next;
};

enum {
    NDSP_WBUF_FREE    = 0,
    NDSP_WBUF_QUEUED  = 1,
    NDSP_WBUF_PLAYING = 2,
    NDSP_WBUF_DONE    = 3
};
#endif

#define BB_AUDIO_CHANNEL          0
#define BB_AUDIO_SFX_CHANNEL      1
#define BB_AUDIO_NUM_BUFS         4
#define BB_AUDIO_SAMPLES_PER_BUF  2048 /* 2048 stereo frames @ 32000 Hz = 64 ms per buffer */
#define BB_SFX_NUM_SLOTS          2
#define BB_SFX_SAMPLES            512

/* WAV validation error codes */
typedef enum {
    BB_WAV_OK = 0,
    BB_WAV_ERR_OPEN = 1,
    BB_WAV_ERR_HEADER = 2,
    BB_WAV_ERR_NOT_PCM = 3,
    BB_WAV_ERR_NOT_STEREO = 4,
    BB_WAV_ERR_NOT_16BIT = 5,
    BB_WAV_ERR_BAD_RATE = 6,
    BB_WAV_ERR_NO_DATA = 7,
    BB_WAV_ERR_TRUNCATED = 8
} bb_wav_status_t;

/* Per-buffer tracking metadata */
typedef struct {
    uint16_t sequence_id;
    uint32_t frame_count;
    uint64_t stream_sample_offset; /* Cumulative sample offset of buffer start */
    bool     is_active;
} bb_wavebuf_meta_t;

typedef struct {
    bool        ndsp_available;
    bool        using_wav_file;
    bool        playing;
    bool        paused;
    bool        eof_reached;

    FILE*       wav_fp;
    uint32_t    wav_data_offset;
    uint32_t    wav_data_bytes;
    uint32_t    wav_bytes_read;
    uint32_t    sample_rate;
    uint32_t    bpm_milli;

    int16_t*    linear_pcm_buf; /* BB_AUDIO_NUM_BUFS * BB_AUDIO_SAMPLES_PER_BUF * 2 int16s */
    ndspWaveBuf wave_bufs[BB_AUDIO_NUM_BUFS];
    bb_wavebuf_meta_t buf_meta[BB_AUDIO_NUM_BUFS];

    /* Monotonic sequence & sample accounting */
    uint16_t    next_sequence_id;
    uint64_t    queued_samples_total;
    uint64_t    completed_samples_retire;
    uint64_t    max_consumed_samples;
    int64_t     monotonic_pos_us;

    /* Lead-in (game_music.gd await_lead_in): silence queued before the song; the reported position runs from -lead_in_us to 0 over it */
    int64_t     pending_lead_in_us;   /* set before bb_audio_start_stream, consumed (and cleared) by it */
    int64_t     lead_in_us;
    uint32_t    lead_in_frames_left;

    /* Fallback monotonic osGetTime() clock if NDSP init fails */
    uint64_t    fallback_start_ms;
    uint64_t    fallback_paused_ms;

    /* Diagnostics */
    uint32_t    active_queued_bufs;
    uint32_t    underrun_count;
    uint32_t    dropped_frames;
    uint32_t    refill_count;

    /* Safe double-buffered procedural SFX subsystem */
    int16_t*    sfx_linear_buf; /* 2 * BB_SFX_SAMPLES * 2 int16s */
    ndspWaveBuf sfx_wave_bufs[BB_SFX_NUM_SLOTS];
    int         sfx_active_slot;
} bb_audio_stream_t;

/* WAV file header validation */
bb_wav_status_t bb_validate_wav_file(
    FILE* fp,
    uint32_t* out_sample_rate,
    uint32_t* out_data_offset,
    uint32_t* out_data_bytes
);

bool    bb_audio_init(bb_audio_stream_t* audio);
void    bb_audio_shutdown(bb_audio_stream_t* audio);

bool    bb_audio_start_stream(bb_audio_stream_t* audio, const char* wav_path, uint32_t bpm_milli);
/* The next bb_audio_start_stream plays `us` of silence first and reports the song position from -us (0 = no lead-in, menus). */
void    bb_audio_set_lead_in(bb_audio_stream_t* audio, int64_t us);
void    bb_audio_stop_stream(bb_audio_stream_t* audio);
void    bb_audio_set_paused(bb_audio_stream_t* audio, bool paused);

/* Pump completed wave buffers and return the current playback position of the SONG in microseconds (negative during a lead-in) */
int64_t bb_audio_update(bb_audio_stream_t* audio);

/* Total length of the streamed WAV in microseconds (0 for the procedural track). */
int64_t bb_audio_duration_us(const bb_audio_stream_t* audio);

/* True once the whole WAV has been consumed by the DSP (or the fallback clock passed its end). */
bool    bb_audio_stream_finished(const bb_audio_stream_t* audio);

/* Playback-rate scale (1.0 = normal); used for the fail "tape stop". Does not affect sample accounting. */
void    bb_audio_set_pitch(bb_audio_stream_t* audio, float pitch);

/* Trigger a short procedural audio cue (0 = hit chime, 1 = miss thud, 2 = shutter click) */
void    bb_audio_trigger_cue(bb_audio_stream_t* audio, int cue_type);

const char* bb_wav_status_name(bb_wav_status_t status);

#endif /* BB_AUDIO_NDSP_H */
