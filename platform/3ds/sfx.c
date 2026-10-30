/**
 * @file sfx.c
 * @brief Stage sound effects (see sfx.h).
 */
#include "sfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd_u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int64_t rd_i64(const uint8_t* p) { return (int64_t)((uint64_t)rd_u32(p) | ((uint64_t)rd_u32(p + 4) << 32)); }

bool bb_sfx_init(bb_sfx_t* s, const char* path, bool ndsp_available) {
    memset(s, 0, sizeof(*s));
    s->ndsp = ndsp_available;
    s->active_loop_bank = -1;
    s->active_voice_bank = -1;

    FILE* fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len < 28 || len > 8 * 1024 * 1024) {
        fclose(fp);
        return false;
    }
    uint8_t* buf = (uint8_t*)malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        free(buf);
        fclose(fp);
        return false;
    }
    fclose(fp);

    bool ok = false;
    const uint8_t* p = buf;
    const uint8_t* end = buf + len;
    if (memcmp(p, "BBSF", 4) != 0) goto done;
    const uint32_t ver = rd_u32(p + 4);
    if (ver != 1 && ver != 2) goto done;
    s->sample_rate = rd_u32(p + 8);
    s->clip_count = rd_u32(p + 12);
    s->loop_count = rd_u32(p + 16);
    s->oneshot_count = rd_u32(p + 20);
    s->voice_count = rd_u32(p + 24);
    p += 28;
    if (s->clip_count > BB_SFX_MAX_CLIPS || s->loop_count > BB_SFX_MAX_LOOPS || s->oneshot_count > BB_SFX_MAX_ONESHOTS ||
        s->voice_count > BB_SFX_MAX_VOICES) {
        goto done;
    }
    if (ver == 1) {
        size_t tables = (size_t)s->clip_count * 40 + (size_t)s->loop_count * 32 + (size_t)s->oneshot_count * 16 + (size_t)s->voice_count * 40;
        if ((size_t)(end - p) < tables) goto done;
    } else if ((size_t)(end - p) < (size_t)s->clip_count * 40) {
        goto done;
    }

    for (uint32_t i = 0; i < s->clip_count; i++, p += 40) {
        memcpy(s->clips[i].name, p, 31);
        s->clips[i].byte_offset = rd_u32(p + 32);
        s->clips[i].frames = rd_u32(p + 36);
    }
    for (uint32_t i = 0; i < s->loop_count; i++) {
        if (end - p < 16) goto done;
        s->loops[i].ts_us = rd_i64(p);
        s->loops[i].mode = rd_u32(p + 8);
        s->loops[i].n = rd_u32(p + 12);
        const uint32_t slots = ver == 1 ? 8u : s->loops[i].n;
        const size_t rec = ver == 1 ? 32u : ((16u + (size_t)slots * 2u + 3u) & ~(size_t)3u);
        if (s->loops[i].n > (ver == 1 ? 8u : (uint32_t)BB_SFX_LOOP_CLIPS) || (size_t)(end - p) < rec) goto done;
        for (uint32_t k = 0; k < slots && k < BB_SFX_LOOP_CLIPS; k++) s->loops[i].clip[k] = rd_u16(p + 16 + k * 2);
        p += rec;
    }
    if ((size_t)(end - p) < (size_t)s->oneshot_count * 16) goto done;
    for (uint32_t i = 0; i < s->oneshot_count; i++, p += 16) {
        s->oneshots[i].ts_us = rd_i64(p);
        s->oneshots[i].clip = rd_u32(p + 8);
    }
    for (uint32_t i = 0; i < s->voice_count; i++) {
        if (end - p < 12) goto done;
        s->voices[i].ts_us = rd_i64(p);
        s->voices[i].n = rd_u32(p + 8);
        const uint32_t slots = ver == 1 ? 12u : s->voices[i].n;
        const size_t rec = ver == 1 ? 40u : ((12u + (size_t)slots * 2u + 3u) & ~(size_t)3u);
        if (s->voices[i].n > (ver == 1 ? 12u : (uint32_t)BB_SFX_VOICE_CLIPS) || (size_t)(end - p) < rec) goto done;
        for (uint32_t k = 0; k < slots && k < BB_SFX_VOICE_CLIPS; k++) s->voices[i].clip[k] = rd_u16(p + 12 + k * 2);
        p += rec;
    }

    size_t blob = (size_t)(end - p);
    for (uint32_t i = 0; i < s->clip_count; i++) {
        if ((size_t)s->clips[i].byte_offset + (size_t)s->clips[i].frames * 2 > blob) goto done;
    }
    s->pcm_bytes = (uint32_t)blob;
    if (s->ndsp && blob > 0) {
        s->pcm = (int16_t*)linearAlloc(blob);
        if (!s->pcm) goto done;
        memcpy(s->pcm, p, blob);
        DSP_FlushDataCache(s->pcm, blob);
        float mix[12];
        for (int c = 0; c < 4; c++) {
            int ch = BB_SFX_CH_LOOP + c;
            memset(mix, 0, sizeof(mix));
            mix[0] = 1.0f;
            mix[1] = 1.0f;
            ndspChnReset(ch);
            ndspChnInitParams(ch);
            ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
            ndspChnSetRate(ch, (float)s->sample_rate);
            ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
            ndspChnSetMix(ch, mix);
        }
    }
    s->loaded = true;
    ok = true;

done:
    free(buf);
    return ok;
}

void bb_sfx_shutdown(bb_sfx_t* s) {
    if (s->pcm) {
        bb_sfx_stop(s);
        linearFree(s->pcm);
        s->pcm = NULL;
    }
    s->loaded = false;
}

void bb_sfx_stop(bb_sfx_t* s) {
    if (s->ndsp) {
        for (int c = 0; c < 4; c++) {
            ndspChnWaveBufClear(BB_SFX_CH_LOOP + c);
        }
    }
}

void bb_sfx_reset(bb_sfx_t* s) {
    bb_sfx_stop(s);
    s->active_loop_bank = -1;
    s->loop_cursor = 0;
    s->active_voice_bank = -1;
    s->voice_cursor = 0;
    s->next_oneshot = 0;
}

static void play_clip(bb_sfx_t* s, int slot, uint32_t clip) {
    s->plays++;
    if (!s->ndsp || !s->pcm || clip >= s->clip_count || s->clips[clip].frames == 0) {
        return;
    }
    int ch = BB_SFX_CH_LOOP + slot;
    ndspChnWaveBufClear(ch);
    ndspWaveBuf* wb = &s->wb[slot];
    memset(wb, 0, sizeof(*wb));
    wb->data_vaddr = (const u8*)s->pcm + s->clips[clip].byte_offset;
    wb->nsamples = s->clips[clip].frames;
    wb->looping = false;
    ndspChnWaveBufAdd(ch, wb);
}

bool bb_sfx_play_named(bb_sfx_t* s, const char* name) {
    if (!s->loaded) {
        return false;
    }
    for (uint32_t i = 0; i < s->clip_count; i++) {
        if (strcmp(s->clips[i].name, name) == 0) {
            play_clip(s, 3, i);
            return true;
        }
    }
    return false;
}

void bb_sfx_on_hit(bb_sfx_t* s, int64_t note_ts_us, bool trigger_voice) {
    if (!s->loaded) {
        return;
    }
    /* Loop-sound bank: latest keyframe at or before the note; a new bank restarts the cycle */
    int bank = -1;
    for (uint32_t i = 0; i < s->loop_count; i++) {
        if (s->loops[i].ts_us <= note_ts_us && (bank < 0 || s->loops[i].ts_us >= s->loops[bank].ts_us)) {
            bank = (int)i;
        }
    }
    if (bank >= 0 && s->loops[bank].n > 0) {
        if (bank != s->active_loop_bank) {
            s->active_loop_bank = bank;
            s->loop_cursor = 0;
        }
        uint32_t n = s->loops[bank].n;
        uint32_t idx = (uint32_t)s->loop_cursor % n;
        play_clip(s, 0, s->loops[bank].clip[idx]);
        s->loop_cursor = (int)((idx + 1) % n);
    }

    if (trigger_voice) {
        int vb = -1;
        for (uint32_t i = 0; i < s->voice_count; i++) {
            if (s->voices[i].ts_us <= note_ts_us && (vb < 0 || s->voices[i].ts_us >= s->voices[vb].ts_us)) {
                vb = (int)i;
            }
        }
        if (vb >= 0 && s->voices[vb].n > 0) {
            if (vb != s->active_voice_bank) {
                s->active_voice_bank = vb;
                s->voice_cursor = 0;
            }
            uint32_t n = s->voices[vb].n;
            uint32_t idx = (uint32_t)s->voice_cursor % n;
            play_clip(s, 1, s->voices[vb].clip[idx]);
            s->voice_cursor = (int)((idx + 1) % n);
        }
    }
}

void bb_sfx_tick(bb_sfx_t* s, int64_t chart_time_us) {
    if (!s->loaded) {
        return;
    }
    while (s->next_oneshot < s->oneshot_count && chart_time_us >= s->oneshots[s->next_oneshot].ts_us) {
        play_clip(s, 2, s->oneshots[s->next_oneshot].clip);
        s->next_oneshot++;
    }
}
