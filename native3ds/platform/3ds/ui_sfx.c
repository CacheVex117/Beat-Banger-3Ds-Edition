/**
 * @file ui_sfx.c
 * @brief Menu feedback sounds (see ui_sfx.h). Plays on NDSP channel 5 (the stage UI channel; stages re-init it on load).
 */
#include "ui_sfx.h"
#include <3ds.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_CH_A 5      /* short feedback sounds alternate between two channels so a new sound does not cut the previous one */
#define UI_CH_B 2
#define UI_CH_VOICE 3  /* Jasmine speech blips */
#define UI_CH_LONG 4   /* results: score counter, cheering (idle while a results screen is up) */
#define MAX_CLIPS 40

static struct { uint32_t offset, frames, rate; } g_clip[MAX_CLIPS];
static int g_clips;
static int16_t* g_pcm;
static ndspWaveBuf g_wb[4];
static int g_short_rr;
static float g_vol = 1.0f;
static bool g_ndsp;

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

bool bb_uisfx_init(bool ndsp_available) {
    g_ndsp = ndsp_available;
    g_clips = 0;
    g_pcm = NULL;
    FILE* f = fopen("romfs:/ui/ui_sfx.bbca", "rb");
    if (!f) {
        return false;
    }
    uint8_t hdr[12];
    bool ok = fread(hdr, 1, 12, f) == 12 && memcmp(hdr, "BBCA", 4) == 0 && rd32(hdr + 8) <= MAX_CLIPS;
    int n = ok ? (int)rd32(hdr + 8) : 0;
    for (int i = 0; ok && i < n; i++) {
        uint8_t e[44];
        if (fread(e, 1, 44, f) != 44) { ok = false; break; }
        g_clip[i].offset = rd32(e + 32);
        g_clip[i].frames = rd32(e + 36);
        g_clip[i].rate = rd32(e + 40);
    }
    if (ok && ndsp_available) {
        long here = ftell(f);
        fseek(f, 0, SEEK_END);
        long total = ftell(f);
        fseek(f, here, SEEK_SET);
        size_t bytes = (size_t)(total - here);
        g_pcm = (int16_t*)linearAlloc(bytes);
        if (!g_pcm || fread(g_pcm, 1, bytes, f) != bytes) {
            if (g_pcm) linearFree(g_pcm);
            g_pcm = NULL;
            ok = false;
        } else {
            DSP_FlushDataCache(g_pcm, bytes);
            for (int ch = UI_CH_B; ch <= UI_CH_A; ch++) {
                ndspChnReset(ch);
                ndspChnInitParams(ch);
                ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
                ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
            }
        }
    }
    fclose(f);
    g_clips = ok ? n : 0;
    return ok;
}

void bb_uisfx_shutdown(void) {
    if (g_ndsp) {
        for (int ch = UI_CH_B; ch <= UI_CH_A; ch++) ndspChnWaveBufClear(ch);
    }
    if (g_pcm) {
        linearFree(g_pcm);
        g_pcm = NULL;
    }
    g_clips = 0;
}

void bb_uisfx_set_volume(float v) { g_vol = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void bb_uisfx_play_pitch(bb_uisfx_t id, float pitch) {
    if (!g_ndsp || !g_pcm || (int)id >= g_clips || g_vol <= 0.0f) {
        return;
    }
    int ch, slot;
    if (id == BB_UISFX_SCORE || id == BB_UISFX_CHEER || id == BB_UISFX_LOW || id == BB_UISFX_WHITE_NOISE || id == BB_UISFX_FAIL_HALLOWEEN) { ch = UI_CH_LONG; slot = 0; }
    else if (id == BB_UISFX_VOICE) { ch = UI_CH_VOICE; slot = 1; }
    else if (g_short_rr++ & 1) { ch = UI_CH_B; slot = 2; }
    else { ch = UI_CH_A; slot = 3; }
    ndspWaveBuf* wb = &g_wb[slot];
    ndspChnWaveBufClear(ch);
    ndspChnSetRate(ch, (float)g_clip[id].rate * pitch);
    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = mix[1] = g_vol;
    ndspChnSetMix(ch, mix);
    memset(wb, 0, sizeof(*wb));
    wb->data_vaddr = (const u8*)g_pcm + g_clip[id].offset;
    wb->nsamples = g_clip[id].frames;
    wb->looping = id == BB_UISFX_WHITE_NOISE;   /* the studio hiss runs under the game-over screen */
    ndspChnWaveBufAdd(ch, wb);
}

void bb_uisfx_play(bb_uisfx_t id) { bb_uisfx_play_pitch(id, 1.0f); }

void bb_uisfx_stop_long(void) {
    if (g_ndsp) ndspChnWaveBufClear(UI_CH_LONG);
}
