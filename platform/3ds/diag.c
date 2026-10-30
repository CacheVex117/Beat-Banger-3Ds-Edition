/**
 * @file diag.c
 * @brief Isolated development diagnostics (see diag.h).
 */
#include "diag.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <3ds.h>
#include <citro3d.h>

#ifdef BB_NO_DIAG

void bb_diag_init(char* out_mode, unsigned mode_cap) { if (out_mode && mode_cap) out_mode[0] = 0; }
void bb_diag_shutdown(void) {}
int  bb_diag_lines(const bb_diag_snapshot_t* s, char lines[][64], int max_lines) { (void)s; (void)lines; (void)max_lines; return 0; }
void bb_diag_log(const char* fmt, ...) { (void)fmt; }
void bb_diag_capture(const char* name) { (void)name; }
void bb_diag_set_sync(bool sync) { (void)sync; }
void bb_diag_flush(void) {}

#else

static FILE* g_log = NULL;
static bool g_sync = true;
static char g_log_buf[32 * 1024];   /* a whole song of telemetry lines (~120 B every 2 s) fits */
static bool g_log_dirty = false;

void bb_diag_init(char* out_mode, unsigned mode_cap) {
    mkdir("sdmc:/bb3ds_evidence", 0777);
    if (out_mode && mode_cap) {
        out_mode[0] = 0;
        FILE* t = fopen("sdmc:/bb3ds_evidence/run_smoke.txt", "r");
        if (t) {
            if (!fgets(out_mode, (int)mode_cap, t)) {
                strncpy(out_mode, "play", mode_cap - 1);
                out_mode[mode_cap - 1] = 0;
            }
            size_t n = strlen(out_mode);
            while (n > 0 && (out_mode[n - 1] == 10 || out_mode[n - 1] == 13 || out_mode[n - 1] == 32)) out_mode[--n] = 0;
            if (n == 0) strncpy(out_mode, "play", mode_cap - 1);
            fclose(t);
        }
    }
    /* append so a crashed session keeps its log; start over once it grows large */
    g_log = fopen("sdmc:/bb3ds_evidence/telemetry.log", "a");
    if (g_log) {
        fseek(g_log, 0, SEEK_END);
        if (ftell(g_log) > 1024 * 1024) {
            fclose(g_log);
            g_log = fopen("sdmc:/bb3ds_evidence/telemetry.log", "w");
        }
        if (g_log) setvbuf(g_log, g_log_buf, _IOFBF, sizeof(g_log_buf));
    }
}

void bb_diag_shutdown(void) {
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}

void bb_diag_log(const char* fmt, ...) {
    if (!g_log) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    if (g_sync) fflush(g_log);
    else g_log_dirty = true;
}

void bb_diag_set_sync(bool sync) {
    g_sync = sync;
    if (sync) bb_diag_flush();
}

void bb_diag_flush(void) {
    if (g_log && g_log_dirty) fflush(g_log);
    g_log_dirty = false;
}

int bb_diag_lines(const bb_diag_snapshot_t* s, char lines[][64], int max_lines) {
    int n = 0;
#define L(...) do { if (n < max_lines) snprintf(lines[n++], 64, __VA_ARGS__); } while (0)
    L("%s  loop %lu shut %lu  ctrl %d", s->state_name, (unsigned long)s->loop_idx, (unsigned long)s->shutters, s->control_mode);
    L("NDSP %s %s %lukHz  bufs %lu U%lu D%lu", s->ndsp_active ? "ACTIVE" : "FALLBACK", s->wav_streaming ? "WAV" : "SYNTH",
      (unsigned long)(s->sample_rate / 1000U), (unsigned long)s->bufs, (unsigned long)s->underruns, (unsigned long)s->dropped);
    L("audio %+.3fs chart %+.3fs cal %+ldms", (double)s->audio_pos_us / 1e6, (double)s->chart_time_us / 1e6, (long)(s->calibration_us / 1000));
    L("score %ld life %ld combo %lu", (long)s->score, (long)s->health, (unsigned long)s->combo);
    L("P%lu G%lu N%lu M%lu X%lu A%lu", (unsigned long)s->perfect, (unsigned long)s->great, (unsigned long)s->nice, (unsigned long)s->meh,
      (unsigned long)s->missed, (unsigned long)s->absent);
    L("last %s %+.1fms", s->last_judgment, s->last_delta_ms);
    L("frame avg %.1f max %.1f ms", s->frame_ms_avg, s->frame_ms_max);
    L("cpu %.0f%% gpu %.0f%% lin %luK", s->cpu_pct, s->gpu_pct, (unsigned long)s->linear_free_kib);
    L("sheets %lu res %luK fail %lu", (unsigned long)s->sheet_loads, (unsigned long)s->resident_kib, (unsigned long)s->loads_failed);
#undef L
    return n;
}

void bb_diag_capture(const char* name) {
    char path[96];
    snprintf(path, sizeof(path), "sdmc:/bb3ds_evidence/%s.ppm", name);
    FILE* fp = fopen(path, "wb");
    if (!fp) {
        return;
    }
    u16 tw = 0, th = 0, bw = 0, bh = 0;
    u8* top_fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &tw, &th);
    u8* bot_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &bw, &bh);
    GSPGPU_FramebufferFormat tf = gfxGetScreenFormat(GFX_TOP);
    GSPGPU_FramebufferFormat bf = gfxGetScreenFormat(GFX_BOTTOM);

    fprintf(fp, "P6\n400 480\n255\n");
    uint8_t row[400 * 3];
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 400; x++) {
            uint8_t r = 0, g = 0, b = 0;
            if (top_fb) {
                int idx = (x * 240) + (239 - y);
                if (tf == GSP_BGR8_OES) {
                    b = top_fb[idx * 3 + 0]; g = top_fb[idx * 3 + 1]; r = top_fb[idx * 3 + 2];
                } else if (tf == GSP_RGBA8_OES) {
                    r = top_fb[idx * 4 + 3]; g = top_fb[idx * 4 + 2]; b = top_fb[idx * 4 + 1];
                } else {
                    u16 px = ((u16*)top_fb)[idx];
                    r = (uint8_t)(((px >> 11) & 0x1F) << 3); g = (uint8_t)(((px >> 5) & 0x3F) << 2); b = (uint8_t)((px & 0x1F) << 3);
                }
            }
            row[x * 3 + 0] = r; row[x * 3 + 1] = g; row[x * 3 + 2] = b;
        }
        fwrite(row, 1, sizeof(row), fp);
    }
    for (int y = 0; y < 240; y++) {
        memset(row, 0x12, sizeof(row));
        for (int x = 0; x < 320; x++) {
            uint8_t r = 0, g = 0, b = 0;
            if (bot_fb) {
                int idx = (x * 240) + (239 - y);
                if (bf == GSP_BGR8_OES) {
                    b = bot_fb[idx * 3 + 0]; g = bot_fb[idx * 3 + 1]; r = bot_fb[idx * 3 + 2];
                } else if (bf == GSP_RGB565_OES) {
                    u16 px = ((u16*)bot_fb)[idx];
                    r = (uint8_t)(((px >> 11) & 0x1F) << 3); g = (uint8_t)(((px >> 5) & 0x3F) << 2); b = (uint8_t)((px & 0x1F) << 3);
                } else {
                    b = bot_fb[idx * 3 + 0]; g = bot_fb[idx * 3 + 1]; r = bot_fb[idx * 3 + 2];
                }
            }
            row[(40 + x) * 3 + 0] = r; row[(40 + x) * 3 + 1] = g; row[(40 + x) * 3 + 2] = b;
        }
        fwrite(row, 1, sizeof(row), fp);
    }
    fclose(fp);
}

#endif
