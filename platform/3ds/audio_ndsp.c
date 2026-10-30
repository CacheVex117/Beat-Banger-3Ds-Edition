/**
 * @file audio_ndsp.c
 * @brief NDSP streaming audio subsystem & monotonic gameplay clock implementation.
 */
#include "audio_ndsp.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const char* bb_wav_status_name(bb_wav_status_t status) {
    switch (status) {
        case BB_WAV_OK:            return "OK";
        case BB_WAV_ERR_OPEN:      return "ERR_OPEN";
        case BB_WAV_ERR_HEADER:    return "ERR_HEADER";
        case BB_WAV_ERR_NOT_PCM:   return "ERR_NOT_PCM";
        case BB_WAV_ERR_NOT_STEREO:return "ERR_NOT_STEREO";
        case BB_WAV_ERR_NOT_16BIT: return "ERR_NOT_16BIT";
        case BB_WAV_ERR_BAD_RATE:  return "ERR_BAD_RATE";
        case BB_WAV_ERR_NO_DATA:   return "ERR_NO_DATA";
        case BB_WAV_ERR_TRUNCATED: return "ERR_TRUNCATED";
        default:                   return "ERR_UNKNOWN";
    }
}

bb_wav_status_t bb_validate_wav_file(
    FILE* fp,
    uint32_t* out_sample_rate,
    uint32_t* out_data_offset,
    uint32_t* out_data_bytes
) {
    if (!fp) {
        return BB_WAV_ERR_OPEN;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        return BB_WAV_ERR_HEADER;
    }
    long file_size = ftell(fp);
    if (file_size < 44) {
        return BB_WAV_ERR_HEADER;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        return BB_WAV_ERR_HEADER;
    }

    uint8_t hdr[12];
    if (fread(hdr, 1, 12, fp) != 12) {
        return BB_WAV_ERR_HEADER;
    }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        return BB_WAV_ERR_HEADER;
    }

    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint16_t block_align = 0;
    uint32_t data_off = 0;
    uint32_t data_len = 0;
    bool found_fmt = false;

    while (1) {
        uint8_t chunk_hdr[8];
        if (fread(chunk_hdr, 1, 8, fp) != 8) {
            break;
        }
        uint32_t chunk_len = (uint32_t)chunk_hdr[4] |
                             ((uint32_t)chunk_hdr[5] << 8) |
                             ((uint32_t)chunk_hdr[6] << 16) |
                             ((uint32_t)chunk_hdr[7] << 24);
        long chunk_pos = ftell(fp);

        if (memcmp(chunk_hdr, "fmt ", 4) == 0 && chunk_len >= 16) {
            uint8_t fmt[16];
            if (fread(fmt, 1, 16, fp) != 16) {
                return BB_WAV_ERR_HEADER;
            }
            audio_format    = (uint16_t)fmt[0] | ((uint16_t)fmt[1] << 8);
            num_channels    = (uint16_t)fmt[2] | ((uint16_t)fmt[3] << 8);
            sample_rate     = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) |
                              ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            block_align     = (uint16_t)fmt[12] | ((uint16_t)fmt[13] << 8);
            bits_per_sample = (uint16_t)fmt[14] | ((uint16_t)fmt[15] << 8);
            found_fmt = true;
        } else if (memcmp(chunk_hdr, "data", 4) == 0) {
            data_off = (uint32_t)chunk_pos;
            data_len = chunk_len;
            break;
        }

        long next_pos = chunk_pos + (long)((chunk_len + 1U) & ~1U);
        if (fseek(fp, next_pos, SEEK_SET) != 0) {
            break;
        }
    }

    if (!found_fmt) {
        return BB_WAV_ERR_HEADER;
    }
    if (audio_format != 1) { /* 1 = uncompressed PCM */
        return BB_WAV_ERR_NOT_PCM;
    }
    if (num_channels != 2) {
        return BB_WAV_ERR_NOT_STEREO;
    }
    if (bits_per_sample != 16) {
        return BB_WAV_ERR_NOT_16BIT;
    }
    if (sample_rate < 8000 || sample_rate > 96000) {
        return BB_WAV_ERR_BAD_RATE;
    }
    if (block_align != 4) {
        return BB_WAV_ERR_HEADER;
    }
    if (data_off == 0 || data_len == 0) {
        return BB_WAV_ERR_NO_DATA;
    }
    if ((long)data_off + (long)data_len > file_size) {
        return BB_WAV_ERR_TRUNCATED;
    }

    if (out_sample_rate) *out_sample_rate = sample_rate;
    if (out_data_offset) *out_data_offset = data_off;
    if (out_data_bytes)  *out_data_bytes  = data_len;
    return BB_WAV_OK;
}

static void fill_procedural_beat_buffer(bb_audio_stream_t* audio, int16_t* dst, uint32_t nsamples) {
    uint32_t sr = audio->sample_rate;
    uint64_t start_sample = audio->queued_samples_total;
    double bpm = (audio->bpm_milli > 0) ? ((double)audio->bpm_milli / 1000.0) : 126.0;
    double samples_per_beat = ((double)sr * 60.0) / bpm;
    double offset_samples = (double)sr * 1.0; /* 1.0s synthetic pre-roll */

    for (uint32_t i = 0; i < nsamples; i++) {
        uint64_t s = start_sample + i;
        double rel = (double)s - offset_samples;
        int16_t val = 0;

        double bass = sin(2.0 * M_PI * 110.0 * ((double)s / (double)sr)) * 2200.0;
        double tick = 0.0;
        if (rel >= 0.0) {
            double beat_pos = fmod(rel, samples_per_beat);
            int beat_idx = (int)(rel / samples_per_beat);
            double env = exp(-beat_pos / ((double)sr * 0.035));
            double freq = (beat_idx % 4 == 0) ? 880.0 : 440.0;
            tick = sin(2.0 * M_PI * freq * (beat_pos / (double)sr)) * 9000.0 * env;
        }

        int32_t mix = (int32_t)(bass + tick);
        if (mix > 32000) mix = 32000;
        if (mix < -32000) mix = -32000;
        val = (int16_t)mix;

        dst[i * 2 + 0] = val;
        dst[i * 2 + 1] = val;
    }
}

static bool fill_and_queue_wavebuf(bb_audio_stream_t* audio, int buf_idx) {
    if (audio->eof_reached) {
        return false;
    }

    ndspWaveBuf* wb = &audio->wave_bufs[buf_idx];
    int16_t* dst = &audio->linear_pcm_buf[buf_idx * BB_AUDIO_SAMPLES_PER_BUF * 2];
    uint32_t frames_to_fill = BB_AUDIO_SAMPLES_PER_BUF;

    if (audio->lead_in_frames_left > 0) {   /* lead-in: silence before the song */
        if (frames_to_fill > audio->lead_in_frames_left) frames_to_fill = audio->lead_in_frames_left;
        memset(dst, 0, frames_to_fill * 4U);
        audio->lead_in_frames_left -= frames_to_fill;
    } else if (audio->using_wav_file && audio->wav_fp) {
        uint32_t bytes_remaining = (audio->wav_data_bytes > audio->wav_bytes_read)
                                   ? (audio->wav_data_bytes - audio->wav_bytes_read)
                                   : 0;
        uint32_t max_bytes = frames_to_fill * 4U;
        uint32_t to_read = (bytes_remaining < max_bytes) ? bytes_remaining : max_bytes;
        if (to_read < 4U) {
            audio->eof_reached = true;
            return false;
        }
        size_t got = fread(dst, 1, to_read, audio->wav_fp);
        if (got < 4U) {
            audio->eof_reached = true;
            return false;
        }
        audio->wav_bytes_read += (uint32_t)got;
        frames_to_fill = (uint32_t)(got / 4U);
        if (audio->wav_bytes_read >= audio->wav_data_bytes) {
            audio->eof_reached = true;
        }
    } else {
        fill_procedural_beat_buffer(audio, dst, frames_to_fill);
    }

#ifdef __3DS__
    DSP_FlushDataCache(dst, frames_to_fill * 4U);
#endif

    memset(wb, 0, sizeof(*wb));
    wb->data_vaddr = dst;
    wb->nsamples = frames_to_fill;
    wb->looping = false;

    uint16_t seq = audio->next_sequence_id++;
    if (audio->next_sequence_id == 0) {
        audio->next_sequence_id = 1; /* Keep sequence IDs nonzero */
    }

    audio->buf_meta[buf_idx].sequence_id = seq;
    audio->buf_meta[buf_idx].frame_count = frames_to_fill;
    audio->buf_meta[buf_idx].stream_sample_offset = audio->queued_samples_total;
    audio->buf_meta[buf_idx].is_active = true;

    audio->queued_samples_total += frames_to_fill;
    audio->refill_count++;

#ifdef __3DS__
    ndspChnWaveBufAdd(BB_AUDIO_CHANNEL, wb);
#else
    wb->sequence_id = seq;
    wb->status = NDSP_WBUF_QUEUED;
#endif
    return true;
}

bool bb_audio_init(bb_audio_stream_t* audio) {
    if (!audio) {
        return false;
    }
    memset(audio, 0, sizeof(*audio));
    audio->sample_rate = 32000;
    audio->bpm_milli = 126000;
    audio->next_sequence_id = 1;

    size_t total_pcm_bytes = (size_t)BB_AUDIO_NUM_BUFS * (size_t)BB_AUDIO_SAMPLES_PER_BUF * 4U;
    size_t sfx_pcm_bytes   = (size_t)BB_SFX_NUM_SLOTS * (size_t)BB_SFX_SAMPLES * 4U;

#ifdef __3DS__
    audio->linear_pcm_buf = (int16_t*)linearAlloc(total_pcm_bytes);
    audio->sfx_linear_buf = (int16_t*)linearAlloc(sfx_pcm_bytes);
#else
    audio->linear_pcm_buf = (int16_t*)malloc(total_pcm_bytes);
    audio->sfx_linear_buf = (int16_t*)malloc(sfx_pcm_bytes);
#endif

    if (!audio->linear_pcm_buf || !audio->sfx_linear_buf) {
        bb_audio_shutdown(audio);
        return false;
    }

#ifdef __3DS__
    Result rc = ndspInit();
    if (R_FAILED(rc)) {
        audio->ndsp_available = false;
        return false;
    }
    audio->ndsp_available = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspSetMasterVol(1.0f);

    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = 1.0f;
    mix[1] = 1.0f;

    ndspChnReset(BB_AUDIO_CHANNEL);
    ndspChnInitParams(BB_AUDIO_CHANNEL);
    ndspChnSetInterp(BB_AUDIO_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(BB_AUDIO_CHANNEL, (float)audio->sample_rate);
    ndspChnSetFormat(BB_AUDIO_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetMix(BB_AUDIO_CHANNEL, mix);

    float sfx_mix[12];
    memset(sfx_mix, 0, sizeof(sfx_mix));
    sfx_mix[0] = 0.65f;
    sfx_mix[1] = 0.65f;
    ndspChnReset(BB_AUDIO_SFX_CHANNEL);
    ndspChnInitParams(BB_AUDIO_SFX_CHANNEL);
    ndspChnSetInterp(BB_AUDIO_SFX_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(BB_AUDIO_SFX_CHANNEL, 32000.0f);
    ndspChnSetFormat(BB_AUDIO_SFX_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetMix(BB_AUDIO_SFX_CHANNEL, sfx_mix);
#else
    audio->ndsp_available = true;
#endif

    return true;
}

void bb_audio_shutdown(bb_audio_stream_t* audio) {
    if (!audio) {
        return;
    }
    bb_audio_stop_stream(audio);

#ifdef __3DS__
    if (audio->ndsp_available) {
        ndspExit();
    }
    if (audio->linear_pcm_buf) {
        linearFree(audio->linear_pcm_buf);
        audio->linear_pcm_buf = NULL;
    }
    if (audio->sfx_linear_buf) {
        linearFree(audio->sfx_linear_buf);
        audio->sfx_linear_buf = NULL;
    }
#else
    if (audio->linear_pcm_buf) {
        free(audio->linear_pcm_buf);
        audio->linear_pcm_buf = NULL;
    }
    if (audio->sfx_linear_buf) {
        free(audio->sfx_linear_buf);
        audio->sfx_linear_buf = NULL;
    }
#endif
    audio->ndsp_available = false;
}

void bb_audio_stop_stream(bb_audio_stream_t* audio) {
    if (!audio) {
        return;
    }
#ifdef __3DS__
    if (audio->ndsp_available) {
        ndspChnWaveBufClear(BB_AUDIO_CHANNEL);
        ndspChnWaveBufClear(BB_AUDIO_SFX_CHANNEL);
        ndspChnSetPaused(BB_AUDIO_CHANNEL, false);
    }
#endif
    if (audio->wav_fp) {
        fclose(audio->wav_fp);
        audio->wav_fp = NULL;
    }
    audio->playing = false;
    audio->paused = false;
    audio->using_wav_file = false;
    audio->active_queued_bufs = 0;
    for (int i = 0; i < BB_AUDIO_NUM_BUFS; i++) {
        audio->buf_meta[i].is_active = false;
    }
    for (int s = 0; s < BB_SFX_NUM_SLOTS; s++) {
        audio->sfx_wave_bufs[s].status = NDSP_WBUF_FREE;
    }
}

void bb_audio_set_lead_in(bb_audio_stream_t* audio, int64_t us) {
    if (audio) audio->pending_lead_in_us = us > 0 ? us : 0;
}

bool bb_audio_start_stream(bb_audio_stream_t* audio, const char* wav_path, uint32_t bpm_milli) {
    if (!audio) {
        return false;
    }
    const int64_t lead_us = audio->pending_lead_in_us;
    audio->pending_lead_in_us = 0;   /* one stream only: scene music never gets a lead-in */
    bb_audio_stop_stream(audio);

    audio->bpm_milli = (bpm_milli > 0) ? bpm_milli : 126000;
    audio->sample_rate = 32000;
    audio->completed_samples_retire = 0;
    audio->queued_samples_total = 0;
    audio->max_consumed_samples = 0;
    audio->monotonic_pos_us = 0;
    audio->underrun_count = 0;
    audio->refill_count = 0;
    audio->eof_reached = false;
    audio->next_sequence_id = 1;
    memset(audio->buf_meta, 0, sizeof(audio->buf_meta));
    memset(audio->wave_bufs, 0, sizeof(audio->wave_bufs));

    if (wav_path && wav_path[0] != '\0') {
        FILE* fp = fopen(wav_path, "rb");
        if (fp) {
            uint32_t sr = 0, data_off = 0, data_len = 0;
            bb_wav_status_t wst = bb_validate_wav_file(fp, &sr, &data_off, &data_len);
            if (wst == BB_WAV_OK) {
                fseek(fp, (long)data_off, SEEK_SET);
                audio->wav_fp = fp;
                audio->wav_data_offset = data_off;
                audio->wav_data_bytes = data_len;
                audio->wav_bytes_read = 0;
                audio->sample_rate = sr;
                audio->using_wav_file = true;
            } else {
                fclose(fp);
            }
        }
    }

    /* whole frames of silence at the stream's rate; the reported clock subtracts exactly that much */
    audio->lead_in_frames_left = (uint32_t)((lead_us * (int64_t)audio->sample_rate) / 1000000LL);
    audio->lead_in_us = (int64_t)audio->lead_in_frames_left * 1000000LL / (int64_t)audio->sample_rate;

    audio->playing = true;
    audio->paused = false;

#ifdef __3DS__
    audio->fallback_start_ms = osGetTime();
    audio->fallback_paused_ms = 0;

    if (audio->ndsp_available) {
        ndspChnWaveBufClear(BB_AUDIO_CHANNEL);
        ndspChnSetRate(BB_AUDIO_CHANNEL, (float)audio->sample_rate);
        ndspChnSetPaused(BB_AUDIO_CHANNEL, false);

        uint32_t queued_ok = 0;
        for (int i = 0; i < BB_AUDIO_NUM_BUFS; i++) {
            if (fill_and_queue_wavebuf(audio, i)) {
                queued_ok++;
            }
        }
        audio->active_queued_bufs = queued_ok;
    }
#else
    uint32_t queued_ok = 0;
    for (int i = 0; i < BB_AUDIO_NUM_BUFS; i++) {
        if (fill_and_queue_wavebuf(audio, i)) {
            queued_ok++;
        }
    }
    audio->active_queued_bufs = queued_ok;
#endif

    return true;
}

void bb_audio_set_paused(bb_audio_stream_t* audio, bool paused) {
    if (!audio || !audio->playing || audio->paused == paused) {
        return;
    }
    audio->paused = paused;

#ifdef __3DS__
    if (audio->ndsp_available) {
        ndspChnSetPaused(BB_AUDIO_CHANNEL, paused);
    } else {
        if (paused) {
            audio->fallback_paused_ms = osGetTime();
        } else if (audio->fallback_paused_ms > 0) {
            uint64_t paused_dur = osGetTime() - audio->fallback_paused_ms;
            audio->fallback_start_ms += paused_dur;
            audio->fallback_paused_ms = 0;
        }
    }
#endif
}

int64_t bb_audio_update(bb_audio_stream_t* audio) {
    if (!audio || !audio->playing) {
        return 0;
    }
    if (audio->paused) {
        return audio->monotonic_pos_us - audio->lead_in_us;
    }

#ifdef __3DS__
    if (!audio->ndsp_available) {
        uint64_t elapsed_ms = osGetTime() - audio->fallback_start_ms;
        int64_t pos_us = (int64_t)(elapsed_ms * 1000ULL);
        if (pos_us > audio->monotonic_pos_us) {
            audio->monotonic_pos_us = pos_us;
        }
        return audio->monotonic_pos_us - audio->lead_in_us;
    }

    audio->dropped_frames = ndspGetDroppedFrames();

    /* 1. Query hardware playback state: current sequence and sample offset */
    bool is_playing = ndspChnIsPlaying(BB_AUDIO_CHANNEL);
    uint16_t cur_seq = is_playing ? ndspChnGetWaveBufSeq(BB_AUDIO_CHANNEL) : 0;
    uint32_t cur_pos = is_playing ? ndspChnGetSamplePos(BB_AUDIO_CHANNEL) : 0;
#else
    bool is_playing = false;
    uint16_t cur_seq = 0;
    uint32_t cur_pos = 0;
#endif

    /* 2. Correlate with active buffer metadata */
    uint64_t consumed_from_seq = 0;
    bool found_active_seq = false;

    if (is_playing && cur_seq != 0) {
        for (int i = 0; i < BB_AUDIO_NUM_BUFS; i++) {
            if (audio->buf_meta[i].is_active && audio->buf_meta[i].sequence_id == cur_seq) {
                uint32_t in_buf = cur_pos;
                if (in_buf > audio->buf_meta[i].frame_count) {
                    in_buf = audio->buf_meta[i].frame_count;
                }
                consumed_from_seq = audio->buf_meta[i].stream_sample_offset + (uint64_t)in_buf;
                found_active_seq = true;
                break;
            }
        }
    }

    /* 3. Scan and recycle completed buffers */
    uint32_t done_this_tick = 0;
    uint32_t active_now = 0;

    for (int i = 0; i < BB_AUDIO_NUM_BUFS; i++) {
        uint8_t st = audio->wave_bufs[i].status;
        if (st == NDSP_WBUF_DONE) {
            uint64_t buf_end_offset = audio->buf_meta[i].stream_sample_offset +
                                      (uint64_t)audio->buf_meta[i].frame_count;
            if (buf_end_offset > audio->completed_samples_retire) {
                audio->completed_samples_retire = buf_end_offset;
            }
            audio->buf_meta[i].is_active = false;
            done_this_tick++;

            if (fill_and_queue_wavebuf(audio, i)) {
                active_now++;
            } else {
                audio->wave_bufs[i].status = NDSP_WBUF_FREE;
            }
        } else if (st == NDSP_WBUF_PLAYING || st == NDSP_WBUF_QUEUED) {
            active_now++;
        }
    }

    /* Underrun detection: all buffers drained while audio remains un-finished */
    if (done_this_tick == BB_AUDIO_NUM_BUFS && !audio->eof_reached) {
        audio->underrun_count++;
    }
    audio->active_queued_bufs = active_now;

    /* 4. Determine authoritative consumed sample total */
    uint64_t candidate = audio->completed_samples_retire;
    if (found_active_seq && consumed_from_seq > candidate) {
        candidate = consumed_from_seq;
    }
    if (candidate > audio->max_consumed_samples) {
        audio->max_consumed_samples = candidate;
    }

    /* Convert to microseconds */
    if (audio->sample_rate > 0) {
        int64_t sample_time_us = (int64_t)((audio->max_consumed_samples * 1000000ULL) / (uint64_t)audio->sample_rate);
        if (sample_time_us > audio->monotonic_pos_us) {
            audio->monotonic_pos_us = sample_time_us;
        }
    }

    return audio->monotonic_pos_us - audio->lead_in_us;   /* the song position: negative during the lead-in */
}

int64_t bb_audio_duration_us(const bb_audio_stream_t* audio) {
    if (!audio || !audio->using_wav_file || audio->sample_rate == 0) {
        return 0;
    }
    return (int64_t)(((uint64_t)audio->wav_data_bytes / 4ULL) * 1000000ULL / (uint64_t)audio->sample_rate);
}

bool bb_audio_stream_finished(const bb_audio_stream_t* audio) {
    int64_t dur = bb_audio_duration_us(audio);
    return audio && audio->playing && dur > 0 && audio->monotonic_pos_us - audio->lead_in_us >= dur;
}

void bb_audio_set_pitch(bb_audio_stream_t* audio, float pitch) {
    if (!audio) {
        return;
    }
    if (pitch < 0.02f) pitch = 0.02f;
    if (pitch > 4.0f) pitch = 4.0f;
#ifdef __3DS__
    if (audio->ndsp_available) {
        ndspChnSetRate(BB_AUDIO_CHANNEL, (float)audio->sample_rate * pitch);
    }
#else
    (void)pitch;
#endif
}

void bb_audio_trigger_cue(bb_audio_stream_t* audio, int cue_type) {
    if (!audio || !audio->ndsp_available || !audio->sfx_linear_buf) {
        return;
    }

    /* Switch to the alternate SFX buffer slot */
    int slot = audio->sfx_active_slot;
    audio->sfx_active_slot = (audio->sfx_active_slot + 1) % BB_SFX_NUM_SLOTS;

    ndspWaveBuf* wb = &audio->sfx_wave_bufs[slot];
    int16_t* pcm = &audio->sfx_linear_buf[slot * BB_SFX_SAMPLES * 2];

#ifdef __3DS__
    /* Wait if this buffer is still busy in hardware (rare since cues are 16ms) */
    while (wb->status == NDSP_WBUF_PLAYING || wb->status == NDSP_WBUF_QUEUED) {
        svcSleepThread(500000); /* 0.5 ms */
    }
#endif

    double freq = (cue_type == 0) ? 1046.5 : ((cue_type == 1) ? 160.0 : 2100.0);
    for (int i = 0; i < BB_SFX_SAMPLES; i++) {
        double env = 1.0 - ((double)i / (double)BB_SFX_SAMPLES);
        env = env * env;
        int16_t s = (int16_t)(sin(2.0 * M_PI * freq * ((double)i / 32000.0)) * 7500.0 * env);
        pcm[i * 2 + 0] = s;
        pcm[i * 2 + 1] = s;
    }

#ifdef __3DS__
    DSP_FlushDataCache(pcm, BB_SFX_SAMPLES * 4U);

    memset(wb, 0, sizeof(*wb));
    wb->data_vaddr = pcm;
    wb->nsamples = BB_SFX_SAMPLES;
    wb->looping = false;
    ndspChnWaveBufAdd(BB_AUDIO_SFX_CHANNEL, wb);
#else
    wb->status = NDSP_WBUF_QUEUED;
#endif
}
