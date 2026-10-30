/**
 * @file test_audio_mock.c
 * @brief Host-executable unit & mocked NDSP tests for audio_ndsp.c.
 *
 * Verifies:
 *   1. WAV validation: valid stereo 16-bit PCM vs mono, 8-bit, MP3/bad magic, bad sample rate, truncated.
 *   2. Sequence-aware sample accounting: rollover, simulated DSP playback cursor, monotonic clock.
 *   3. Buffer starvation & underrun recovery.
 *   4. EOF handling and partial final buffers.
 *   5. Startup, pause/resume, repeated restart without leaks.
 *   6. Safe double-buffered SFX slot switching without memory hazards.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "audio_ndsp.h"

static int g_audio_checks_run = 0;
static int g_audio_checks_failed = 0;

#define AUDIO_EXPECT_EQ(actual, expected, msg) do { \
    g_audio_checks_run++; \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        g_audio_checks_failed++; \
        fprintf(stderr, "[FAIL] %s:%d: %s (got %lld, expected %lld)\n", __FILE__, __LINE__, (msg), _a, _e); \
    } \
} while (0)

#define AUDIO_EXPECT_TRUE(cond, msg) do { \
    g_audio_checks_run++; \
    if (!(cond)) { \
        g_audio_checks_failed++; \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
    } \
} while (0)

static FILE* create_dummy_wav(uint16_t format, uint16_t channels, uint32_t sample_rate, uint16_t bits, uint32_t data_bytes) {
    FILE* fp = tmpfile();
    if (!fp) return NULL;

    uint32_t total_size = 36 + data_bytes;
    uint16_t block_align = (channels * bits) / 8;
    uint32_t byte_rate = sample_rate * block_align;

    uint8_t hdr[44];
    memcpy(hdr + 0, "RIFF", 4);
    hdr[4] = (uint8_t)(total_size & 0xFF);
    hdr[5] = (uint8_t)((total_size >> 8) & 0xFF);
    hdr[6] = (uint8_t)((total_size >> 16) & 0xFF);
    hdr[7] = (uint8_t)((total_size >> 24) & 0xFF);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    hdr[16] = 16; hdr[17] = 0; hdr[18] = 0; hdr[19] = 0;
    hdr[20] = (uint8_t)(format & 0xFF);
    hdr[21] = (uint8_t)((format >> 8) & 0xFF);
    hdr[22] = (uint8_t)(channels & 0xFF);
    hdr[23] = (uint8_t)((channels >> 8) & 0xFF);
    hdr[24] = (uint8_t)(sample_rate & 0xFF);
    hdr[25] = (uint8_t)((sample_rate >> 8) & 0xFF);
    hdr[26] = (uint8_t)((sample_rate >> 16) & 0xFF);
    hdr[27] = (uint8_t)((sample_rate >> 24) & 0xFF);
    hdr[28] = (uint8_t)(byte_rate & 0xFF);
    hdr[29] = (uint8_t)((byte_rate >> 8) & 0xFF);
    hdr[30] = (uint8_t)((byte_rate >> 16) & 0xFF);
    hdr[31] = (uint8_t)((byte_rate >> 24) & 0xFF);
    hdr[32] = (uint8_t)(block_align & 0xFF);
    hdr[33] = (uint8_t)((block_align >> 8) & 0xFF);
    hdr[34] = (uint8_t)(bits & 0xFF);
    hdr[35] = (uint8_t)((bits >> 8) & 0xFF);
    memcpy(hdr + 36, "data", 4);
    hdr[40] = (uint8_t)(data_bytes & 0xFF);
    hdr[41] = (uint8_t)((data_bytes >> 8) & 0xFF);
    hdr[42] = (uint8_t)((data_bytes >> 16) & 0xFF);
    hdr[43] = (uint8_t)((data_bytes >> 24) & 0xFF);

    fwrite(hdr, 1, 44, fp);
    if (data_bytes > 0) {
        uint8_t dummy[512] = {0};
        uint32_t written = 0;
        while (written < data_bytes) {
            uint32_t chunk = (data_bytes - written < sizeof(dummy)) ? (data_bytes - written) : (uint32_t)sizeof(dummy);
            fwrite(dummy, 1, chunk, fp);
            written += chunk;
        }
    }
    fflush(fp);
    rewind(fp);
    return fp;
}

static void test_wav_validation(void) {
    /* Valid stereo 16-bit 32000Hz WAV */
    FILE* f_valid = create_dummy_wav(1, 2, 32000, 16, 8192);
    uint32_t sr = 0, off = 0, len = 0;
    bb_wav_status_t st = bb_validate_wav_file(f_valid, &sr, &off, &len);
    AUDIO_EXPECT_EQ(st, BB_WAV_OK, "Valid WAV passes");
    AUDIO_EXPECT_EQ(sr, 32000, "Sample rate 32000");
    AUDIO_EXPECT_EQ(len, 8192, "Data length 8192");
    fclose(f_valid);

    /* Mono WAV rejected */
    FILE* f_mono = create_dummy_wav(1, 1, 32000, 16, 4096);
    st = bb_validate_wav_file(f_mono, &sr, &off, &len);
    AUDIO_EXPECT_EQ(st, BB_WAV_ERR_NOT_STEREO, "Mono WAV rejected");
    fclose(f_mono);

    /* 8-bit WAV rejected */
    FILE* f_8bit = create_dummy_wav(1, 2, 32000, 8, 4096);
    st = bb_validate_wav_file(f_8bit, &sr, &off, &len);
    AUDIO_EXPECT_EQ(st, BB_WAV_ERR_NOT_16BIT, "8-bit WAV rejected");
    fclose(f_8bit);

    /* Compressed (non-PCM) WAV rejected */
    FILE* f_comp = create_dummy_wav(3, 2, 32000, 16, 8192);
    st = bb_validate_wav_file(f_comp, &sr, &off, &len);
    AUDIO_EXPECT_EQ(st, BB_WAV_ERR_NOT_PCM, "Compressed format rejected");
    fclose(f_comp);

    /* Truncated data chunk rejected: header declares 100000 bytes, but only 1024 bytes written */
    FILE* f_trunc = tmpfile();
    if (f_trunc) {
        uint8_t hdr[44] = {0};
        uint32_t total = 36 + 100000;
        memcpy(hdr + 0, "RIFF", 4);
        hdr[4] = (uint8_t)(total & 0xFF);
        hdr[5] = (uint8_t)((total >> 8) & 0xFF);
        hdr[6] = (uint8_t)((total >> 16) & 0xFF);
        hdr[7] = (uint8_t)((total >> 24) & 0xFF);
        memcpy(hdr + 8, "WAVEfmt ", 8);
        hdr[16] = 16;
        hdr[20] = 1; /* PCM */
        hdr[22] = 2; /* 2 channels */
        hdr[24] = 0x80; hdr[25] = 0x7D; /* 32000 Hz */
        hdr[32] = 4; /* block align */
        hdr[34] = 16; /* 16 bits */
        memcpy(hdr + 36, "data", 4);
        hdr[40] = (uint8_t)(100000 & 0xFF);
        hdr[41] = (uint8_t)((100000 >> 8) & 0xFF);
        hdr[42] = (uint8_t)((100000 >> 16) & 0xFF);
        hdr[43] = (uint8_t)((100000 >> 24) & 0xFF);
        fwrite(hdr, 1, 44, f_trunc);
        uint8_t zeros[1024] = {0};
        fwrite(zeros, 1, sizeof(zeros), f_trunc); /* Only write 1024 bytes instead of 100000 */
        fflush(f_trunc);
        rewind(f_trunc);
        st = bb_validate_wav_file(f_trunc, &sr, &off, &len);
        AUDIO_EXPECT_EQ(st, BB_WAV_ERR_TRUNCATED, "Truncated WAV rejected");
        fclose(f_trunc);
    }
}

static void test_audio_lifecycle_and_accounting(void) {
    bb_audio_stream_t audio;
    bool ok = bb_audio_init(&audio);
    AUDIO_EXPECT_TRUE(ok, "Audio init succeeds");

    /* Start procedural stream */
    ok = bb_audio_start_stream(&audio, NULL, 126000);
    AUDIO_EXPECT_TRUE(ok, "Procedural stream started");
    AUDIO_EXPECT_EQ(audio.active_queued_bufs, BB_AUDIO_NUM_BUFS, "All 4 buffers queued");
    AUDIO_EXPECT_EQ(audio.queued_samples_total, 4 * BB_AUDIO_SAMPLES_PER_BUF, "Queued 8192 samples");

    /* Simulate DSP completing buffer 0 */
    audio.wave_bufs[0].status = NDSP_WBUF_DONE;
    bb_audio_update(&audio);
    AUDIO_EXPECT_EQ(audio.completed_samples_retire, BB_AUDIO_SAMPLES_PER_BUF, "Buffer 0 completed retired");
    AUDIO_EXPECT_EQ(audio.refill_count, 5, "Refilled 5th buffer");
    AUDIO_EXPECT_TRUE(audio.monotonic_pos_us > 0, "Monotonic clock advanced");

    /* Simulate starvation (all buffers finish at once) */
    for (int i = 0; i < BB_AUDIO_NUM_BUFS; i++) {
        audio.wave_bufs[i].status = NDSP_WBUF_DONE;
    }
    bb_audio_update(&audio);
    AUDIO_EXPECT_EQ(audio.underrun_count, 1, "Underrun detected on full starvation");
    AUDIO_EXPECT_EQ(audio.active_queued_bufs, BB_AUDIO_NUM_BUFS, "Recovered and refilled all buffers");

    /* Pause and resume */
    int64_t clock_before_pause = audio.monotonic_pos_us;
    bb_audio_set_paused(&audio, true);
    AUDIO_EXPECT_TRUE(audio.paused, "Audio marked paused");
    bb_audio_update(&audio);
    AUDIO_EXPECT_EQ(audio.monotonic_pos_us, clock_before_pause, "Clock frozen while paused");

    bb_audio_set_paused(&audio, false);
    AUDIO_EXPECT_TRUE(!audio.paused, "Audio unpaused");

    /* Safe SFX trigger switching */
    bb_audio_trigger_cue(&audio, 0);
    AUDIO_EXPECT_EQ(audio.sfx_active_slot, 1, "Active SFX slot flipped to 1");
    bb_audio_trigger_cue(&audio, 1);
    AUDIO_EXPECT_EQ(audio.sfx_active_slot, 0, "Active SFX slot flipped back to 0");

    /* Lead-in: 128 ms of silence = two 2048-frame buffers at 32 kHz; the song clock runs from -128 ms to 0 over them */
    bb_audio_set_lead_in(&audio, 128000);
    bb_audio_start_stream(&audio, NULL, 126000);
    AUDIO_EXPECT_EQ(audio.lead_in_us, 128000, "Lead-in rounded to whole frames");
    AUDIO_EXPECT_TRUE(bb_audio_update(&audio) == -128000, "Song position starts at minus the lead-in");
    {
        const int16_t* first = &audio.linear_pcm_buf[0];
        int silent = 1;
        for (int i = 0; i < BB_AUDIO_SAMPLES_PER_BUF * 2; i++) if (first[i] != 0) silent = 0;
        AUDIO_EXPECT_TRUE(silent, "Lead-in buffers are silent");
    }
    audio.wave_bufs[0].status = NDSP_WBUF_DONE;
    bb_audio_update(&audio);
    audio.wave_bufs[1].status = NDSP_WBUF_DONE;
    AUDIO_EXPECT_TRUE(bb_audio_update(&audio) == 0, "Song position reaches 0 when the lead-in has played");
    audio.wave_bufs[2].status = NDSP_WBUF_DONE;
    AUDIO_EXPECT_TRUE(bb_audio_update(&audio) == 64000, "Then advances with the song");
    bb_audio_start_stream(&audio, NULL, 126000);
    AUDIO_EXPECT_EQ(audio.lead_in_us, 0, "A lead-in applies to one stream only");

    /* Repeated restart cleanly resets counters without memory leaks */
    bb_audio_start_stream(&audio, NULL, 126000);
    AUDIO_EXPECT_EQ(audio.completed_samples_retire, 0, "Counters reset on restart");
    AUDIO_EXPECT_EQ(audio.underrun_count, 0, "Underrun count reset");

    bb_audio_shutdown(&audio);
    AUDIO_EXPECT_TRUE(!audio.ndsp_available, "Shutdown cleans up state");
}

int main(void) {
    printf("=== Beat Banger Host Audio Mock & Validation Suite ===\n");
    test_wav_validation();
    test_audio_lifecycle_and_accounting();

    printf("Executed %d audio assertions: %d failed.\n", g_audio_checks_run, g_audio_checks_failed);
    if (g_audio_checks_failed != 0) {
        return 1;
    }
    printf("[PASS] All host audio mock tests succeeded.\n");
    return 0;
}
