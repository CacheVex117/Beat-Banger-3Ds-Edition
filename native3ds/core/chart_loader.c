/**
 * @file chart_loader.c
 * @brief Portable binary (.bbchart) and synthetic chart loader implementation.
 */
#include "chart_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int64_t read_i64_le(const uint8_t* p) {
    uint64_t u = (uint64_t)p[0] |
                 ((uint64_t)p[1] << 8) |
                 ((uint64_t)p[2] << 16) |
                 ((uint64_t)p[3] << 24) |
                 ((uint64_t)p[4] << 32) |
                 ((uint64_t)p[5] << 40) |
                 ((uint64_t)p[6] << 48) |
                 ((uint64_t)p[7] << 56);
    return (int64_t)u;
}

static void copy_fixed_str(char* dst, size_t dst_cap, const uint8_t* src, size_t src_len) {
    size_t n = (src_len < (dst_cap - 1)) ? src_len : (dst_cap - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bb_load_status_t parse_chart(bb_chart_t* out_chart, const uint8_t* data, size_t size) {
    if (!out_chart || !data) {
        return BB_LOAD_ERR_NULL_ARG;
    }
    if (size < 152) {
        return BB_LOAD_ERR_FILE_READ;
    }

    uint32_t magic = read_u32_le(data + 0);
    if (magic != BB_CHART_MAGIC) {
        return BB_LOAD_ERR_BAD_MAGIC;
    }

    uint32_t version = read_u32_le(data + 4);
    if (version != BB_CHART_VERSION) {
        return BB_LOAD_ERR_BAD_VERSION;
    }

    memset(out_chart, 0, sizeof(*out_chart));
    copy_fixed_str(out_chart->stage_id, sizeof(out_chart->stage_id), data + 8, 32);
    copy_fixed_str(out_chart->stage_title, sizeof(out_chart->stage_title), data + 40, 48);
    copy_fixed_str(out_chart->difficulty, sizeof(out_chart->difficulty), data + 88, 24);

    out_chart->rating = (uint8_t)read_u32_le(data + 112);
    out_chart->initial_bpm_milli = read_u32_le(data + 116);
    if (out_chart->initial_bpm_milli == 0) {
        return BB_LOAD_ERR_BAD_BPM;
    }

    out_chart->song_offset_us = read_i64_le(data + 120);
    out_chart->note_offset_us = read_i64_le(data + 128);

    uint32_t note_count = read_u32_le(data + 136);
    uint32_t shutter_count = read_u32_le(data + 140);
    uint32_t loop_count = read_u32_le(data + 144);
    uint32_t bpm_change_count = read_u32_le(data + 148);

    if (note_count > BB_MAX_NOTES ||
        shutter_count > BB_MAX_SHUTTERS ||
        loop_count > BB_MAX_LOOPS ||
        bpm_change_count > BB_MAX_BPM_CHANGES) {
        return BB_LOAD_ERR_TOO_MANY_ITEMS;
    }

    size_t expected_size = 152ULL +
                           (size_t)note_count * 16ULL +
                           (size_t)shutter_count * 8ULL +
                           (size_t)loop_count * 40ULL +
                           (size_t)bpm_change_count * 16ULL;
    if (size < expected_size) {
        return BB_LOAD_ERR_FILE_READ;
    }

    size_t offset = 152;
    out_chart->note_count = note_count;
    for (uint32_t i = 0; i < note_count; i++) {
        int64_t ts = read_i64_le(data + offset);
        uint8_t in_type = data[offset + 8];
        uint8_t mod = data[offset + 9];
        uint8_t voice = data[offset + 10];
        offset += 16;

        if (in_type >= BB_INPUT_COUNT || mod > BB_MOD_HOLD) {
            return BB_LOAD_ERR_INVALID_INPUT_TYPE;
        }
        /* Reserved bytes 11..14: hold duration in microseconds (u32), only for HOLD notes */
        uint32_t hold_dur = read_u32_le(data + offset - 16 + 11);
        if ((mod == BB_MOD_HOLD && hold_dur == 0) || (mod != BB_MOD_HOLD && hold_dur != 0)) {
            return BB_LOAD_ERR_BAD_HOLD;
        }
        if (i > 0 && ts < out_chart->notes[i - 1].timestamp_us) {
            return BB_LOAD_ERR_UNSORTED_NOTES;
        }

        out_chart->notes[i].timestamp_us = ts;
        out_chart->notes[i].input_type = in_type;
        out_chart->notes[i].modifier = mod;
        out_chart->notes[i].trigger_voice = voice;
        out_chart->notes[i].active = 1;
        out_chart->notes[i].holding = 0;
        out_chart->notes[i].hold_end_us = (mod == BB_MOD_HOLD) ? ts + (int64_t)hold_dur : 0;
        out_chart->notes[i].result = BB_JUDGE_NONE;
        out_chart->notes[i].hit_delta_us = 0;
    }

    out_chart->shutter_count = shutter_count;
    for (uint32_t i = 0; i < shutter_count; i++) {
        int64_t ts = read_i64_le(data + offset);
        offset += 8;
        if (i > 0 && ts < out_chart->shutter_times_us[i - 1]) {
            return BB_LOAD_ERR_UNSORTED_SHUTTERS;
        }
        out_chart->shutter_times_us[i] = ts;
    }

    out_chart->loop_count = loop_count;
    for (uint32_t i = 0; i < loop_count; i++) {
        out_chart->loops[i].timestamp_us = read_i64_le(data + offset);
        if (i > 0 && out_chart->loops[i].timestamp_us < out_chart->loops[i - 1].timestamp_us) {
            return BB_LOAD_ERR_UNSORTED_LOOPS;
        }
        copy_fixed_str(out_chart->loops[i].name, sizeof(out_chart->loops[i].name), data + offset + 8, 32);
        offset += 40;
    }

    out_chart->bpm_change_count = bpm_change_count;
    for (uint32_t i = 0; i < bpm_change_count; i++) {
        out_chart->bpm_changes[i].timestamp_us = read_i64_le(data + offset);
        out_chart->bpm_changes[i].bpm_milli = read_u32_le(data + offset + 8);
        if (out_chart->bpm_changes[i].bpm_milli == 0) {
            return BB_LOAD_ERR_BAD_BPM;
        }
        if (i > 0 && out_chart->bpm_changes[i].timestamp_us < out_chart->bpm_changes[i - 1].timestamp_us) {
            return BB_LOAD_ERR_UNSORTED_BPM;
        }
        offset += 16;
    }

    return BB_LOAD_OK;
}

bb_load_status_t bb_chart_load_from_memory(bb_chart_t* out_chart, const uint8_t* data, size_t size) {
    bb_load_status_t st = parse_chart(out_chart, data, size);
    if (st != BB_LOAD_OK && out_chart && st != BB_LOAD_ERR_NULL_ARG) {
        memset(out_chart, 0, sizeof(*out_chart)); /* never expose a half-parsed chart */
    }
    return st;
}

bb_load_status_t bb_chart_load_from_file(bb_chart_t* out_chart, const char* path) {
    if (!out_chart || !path) {
        return BB_LOAD_ERR_NULL_ARG;
    }
    FILE* f = fopen(path, "rb");
    if (!f) {
        return BB_LOAD_ERR_FILE_OPEN;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return BB_LOAD_ERR_FILE_READ;
    }
    long len = ftell(f);
    if (len < 152 || len > (1024 * 1024)) {
        fclose(f);
        return BB_LOAD_ERR_FILE_READ;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return BB_LOAD_ERR_FILE_READ;
    }

    uint8_t* buf = (uint8_t*)malloc((size_t)len);
    if (!buf) {
        fclose(f);
        return BB_LOAD_ERR_FILE_READ;
    }
    size_t read_bytes = fread(buf, 1, (size_t)len, f);
    fclose(f);

    if (read_bytes != (size_t)len) {
        free(buf);
        return BB_LOAD_ERR_FILE_READ;
    }

    bb_load_status_t st = bb_chart_load_from_memory(out_chart, buf, read_bytes);
    free(buf);
    return st;
}

void bb_chart_build_synthetic(bb_chart_t* out_chart) {
    if (!out_chart) {
        return;
    }
    memset(out_chart, 0, sizeof(*out_chart));
    strcpy(out_chart->stage_id, "synthetic_test");
    strcpy(out_chart->stage_title, "Synthetic Calibration Track");
    strcpy(out_chart->difficulty, "Test (A/B)");
    out_chart->rating = 0;
    out_chart->initial_bpm_milli = 126000; /* 126.0 BPM -> 476,190 us per beat */
    out_chart->song_offset_us = 1000000;   /* 1.0 s pre-roll */
    out_chart->note_offset_us = 0;

    out_chart->bpm_change_count = 1;
    out_chart->bpm_changes[0].timestamp_us = 0;
    out_chart->bpm_changes[0].bpm_milli = 126000;

    /* 32 quarter-note beats at 126 BPM (~15.2 seconds) alternating A and B patterns */
    out_chart->note_count = 32;
    for (uint32_t i = 0; i < out_chart->note_count; i++) {
        int64_t beat_us = ((int64_t)i * 60000000LL) / 126LL;
        out_chart->notes[i].timestamp_us = beat_us;
        out_chart->notes[i].input_type = (uint8_t)((i % 4 == 2 || i % 4 == 3) ? BB_INPUT_B : BB_INPUT_A);
        out_chart->notes[i].modifier = BB_MOD_NORMAL;
        out_chart->notes[i].trigger_voice = (uint8_t)((i % 8 == 0) ? 1 : 0);
        out_chart->notes[i].active = 1;
    }

    out_chart->shutter_count = 3;
    out_chart->shutter_times_us[0] = (8LL * 60000000LL) / 126LL;
    out_chart->shutter_times_us[1] = (16LL * 60000000LL) / 126LL;
    out_chart->shutter_times_us[2] = (24LL * 60000000LL) / 126LL;

    out_chart->loop_count = 3;
    out_chart->loops[0].timestamp_us = 0;
    strcpy(out_chart->loops[0].name, "synth_section_1");
    out_chart->loops[1].timestamp_us = out_chart->shutter_times_us[0];
    strcpy(out_chart->loops[1].name, "synth_section_2");
    out_chart->loops[2].timestamp_us = out_chart->shutter_times_us[1];
    strcpy(out_chart->loops[2].name, "synth_section_3");
}

const char* bb_load_status_name(bb_load_status_t status) {
    switch (status) {
        case BB_LOAD_OK:                     return "OK";
        case BB_LOAD_ERR_NULL_ARG:           return "ERR_NULL_ARG";
        case BB_LOAD_ERR_FILE_OPEN:          return "ERR_FILE_OPEN";
        case BB_LOAD_ERR_FILE_READ:          return "ERR_FILE_READ";
        case BB_LOAD_ERR_BAD_MAGIC:          return "ERR_BAD_MAGIC";
        case BB_LOAD_ERR_BAD_VERSION:        return "ERR_BAD_VERSION";
        case BB_LOAD_ERR_BAD_BPM:            return "ERR_BAD_BPM";
        case BB_LOAD_ERR_TOO_MANY_ITEMS:     return "ERR_TOO_MANY_ITEMS";
        case BB_LOAD_ERR_UNSORTED_NOTES:     return "ERR_UNSORTED_NOTES";
        case BB_LOAD_ERR_INVALID_INPUT_TYPE: return "ERR_INVALID_INPUT_TYPE";
        case BB_LOAD_ERR_UNSORTED_SHUTTERS:  return "ERR_UNSORTED_SHUTTERS";
        case BB_LOAD_ERR_BAD_HOLD:           return "ERR_BAD_HOLD";
        case BB_LOAD_ERR_UNSORTED_LOOPS:     return "ERR_UNSORTED_LOOPS";
        case BB_LOAD_ERR_UNSORTED_BPM:       return "ERR_UNSORTED_BPM";
        default:                             return "ERR_UNKNOWN";
    }
}
