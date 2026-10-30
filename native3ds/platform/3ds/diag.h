/**
 * @file diag.h
 * @brief Isolated development diagnostics: text overlay, SD telemetry log and
 *        screen capture. Everything here is optional; build with -DBB_NO_DIAG to compile it
 *        out. The bottom screen is reserved for the user's own UI and is only written here.
 */
#ifndef BB_DIAG_H
#define BB_DIAG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int         app_state;
    const char* state_name;
    bool        ndsp_active;
    bool        wav_streaming;
    uint32_t    sample_rate;
    uint64_t    consumed_samples;
    uint32_t    bufs, underruns, dropped;
    int64_t     audio_pos_us, chart_time_us, calibration_us;
    int32_t     score, health;
    uint32_t    combo, perfect, great, nice, meh, missed, absent, registered;
    const char* last_judgment;
    double      last_delta_ms;
    double      frame_ms_avg, frame_ms_max;
    uint32_t    sheet_loads, last_load_ms, resident_kib, loads_failed;
    uint32_t    loop_idx, shutters;
    double      cpu_pct, gpu_pct;
    uint32_t    linear_free_kib;
    int         control_mode;
} bb_diag_snapshot_t;

/* out_mode receives the first line of sdmc:/bb3ds_evidence/run_smoke.txt ("play", "fail zoe_e1 expert", ...) or "" */
void bb_diag_init(char* out_mode, unsigned mode_cap);
void bb_diag_shutdown(void);
/* Format the overlay into up to max_lines lines of 64 chars; returns the line count. */
int  bb_diag_lines(const bb_diag_snapshot_t* s, char lines[][64], int max_lines);
/* Append one line to sdmc:/bb3ds_evidence/telemetry.log */
void bb_diag_log(const char* fmt, ...);
/* Sync on (smoke runs): every line is flushed to the SD card at once, so a crash keeps it. Sync off (players): lines collect in a
 * RAM buffer and reach the card only at bb_diag_flush(), called outside gameplay - an SD write mid-song can stall a frame. */
void bb_diag_set_sync(bool sync);
void bb_diag_flush(void);
/* Dump both screens to sdmc:/bb3ds_evidence/<name>.ppm (400x480) */
void bb_diag_capture(const char* name);

#endif
