/**
 * @file chart_loader.h
 * @brief Portable binary (.bbchart) and synthetic chart loader (pure C99, no <3ds.h>).
 *
 * Binary format "BBCH" v1 (little-endian):
 *   Offset  Size  Field
 *   0       4     magic ("BBCH")
 *   4       4     version (1)
 *   8       32    stage_id (null-terminated ASCII)
 *   40      48    stage_title (null-terminated ASCII)
 *   88      24    difficulty (null-terminated ASCII)
 *   112     4     rating (uint32)
 *   116     4     initial_bpm_milli (uint32, e.g. 126000)
 *   120     8     song_offset_us (int64)
 *   128     8     note_offset_us (int64)
 *   136     4     note_count (uint32)
 *   140     4     shutter_count (uint32)
 *   144     4     loop_count (uint32)
 *   148     4     bpm_change_count (uint32)
 *   152     ...   note_count * 16-byte note records:
 *                   int64_t timestamp_us
 *                   uint8_t input_type
 *                   uint8_t modifier
 *                   uint8_t trigger_voice
 *                   uint8_t reserved0 (0)
 *                   uint32_t hold_duration_us  (bytes 11..14; non-zero iff modifier == 3 HOLD;
 *                                               hold_end = timestamp + duration. v1 files without
 *                                               holds have zeros here, so the version is unchanged)
 *                   uint8_t reserved1 (0)
 *   ...           shutter_count * 8-byte int64_t shutter_times_us
 *   ...           loop_count * 40-byte loop records:
 *                   int64_t timestamp_us
 *                   char    name[32]
 *   ...           bpm_change_count * 16-byte bpm records:
 *                   int64_t  timestamp_us
 *                   uint32_t bpm_milli
 *                   uint32_t reserved
 */
#ifndef BB_CHART_LOADER_H
#define BB_CHART_LOADER_H

#include "rhythm_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BB_CHART_MAGIC      0x48434242U /* "BBCH" in little-endian */
#define BB_CHART_VERSION    1U

typedef enum {
    BB_LOAD_OK = 0,
    BB_LOAD_ERR_NULL_ARG = 1,
    BB_LOAD_ERR_FILE_OPEN = 2,
    BB_LOAD_ERR_FILE_READ = 3,
    BB_LOAD_ERR_BAD_MAGIC = 4,
    BB_LOAD_ERR_BAD_VERSION = 5,
    BB_LOAD_ERR_BAD_BPM = 6,
    BB_LOAD_ERR_TOO_MANY_ITEMS = 7,
    BB_LOAD_ERR_UNSORTED_NOTES = 8,
    BB_LOAD_ERR_INVALID_INPUT_TYPE = 9,
    BB_LOAD_ERR_UNSORTED_SHUTTERS = 10,
    BB_LOAD_ERR_BAD_HOLD = 11,
    BB_LOAD_ERR_UNSORTED_LOOPS = 12,
    BB_LOAD_ERR_UNSORTED_BPM = 13
} bb_load_status_t;

/* Parse a .bbchart from a memory buffer */
bb_load_status_t bb_chart_load_from_memory(bb_chart_t* out_chart, const uint8_t* data, size_t size);

/* Load a .bbchart from a filesystem path (stdio FILE*) */
bb_load_status_t bb_chart_load_from_file(bb_chart_t* out_chart, const char* path);

/* Populate a deterministic synthetic test chart (126 BPM, A/B notes + shutters + loops) */
void bb_chart_build_synthetic(bb_chart_t* out_chart);

/* Human-readable status string */
const char* bb_load_status_name(bb_load_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* BB_CHART_LOADER_H */
