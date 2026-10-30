/**
 * @file save_data.h
 * @brief Persistent settings + progression (pure C99, no I/O). The 3DS layer reads/writes the byte image.
 *
 * Mirrors the reference's save model (class/save.gd): per level and difficulty the best score / grade / max combo
 * are kept, and each act has `levels_unlocked` (completing level k unlocks level k+1). Settings cover control mode,
 * calibration, scroll speed (GameManager.note_speed_mod_values) and volumes.
 *
 * Byte image (little-endian): magic "BBSV", version, payload size, crc32 of payload, payload. Anything that fails
 * validation is rejected and defaults are used, so a damaged file can never feed bad values into the game.
 */
#ifndef BB_SAVE_DATA_H
#define BB_SAVE_DATA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "input_map.h"

#define BB_SAVE_MAGIC        0x56534242u /* "BBSV" */
#define BB_SAVE_VERSION      7u  /* v1 (no player block), v2 (no master/voice volume), v3 (no cutscene toggle), v4 (no reduce motion), v5 (no scroll mode) and v6 (no theme) images still load */
#define BB_SAVE_MAX_STAGES   64   /* = main.c MAX_STAGES (built-in + SD mods); the image is count-prefixed, so older saves load as-is */
#define BB_SAVE_MAX_ACTS     32
#define BB_SAVE_ID_LEN       24
#define BB_SAVE_MAX_BYTES    8192
#define BB_MAX_ITEMS         160
#define BB_MAX_ACHS          32
#define BB_STAT_COUNT        10   /* items.gd STATS 801..810 */
#define BB_SCROLL_STEPS      7   /* 1, 1.5, 2, 3, 4, 5, 6 x */
#define BB_CAL_LIMIT_MS      200

typedef struct {
    uint8_t  control_mode;   /* bb_control_mode_t */
    int16_t  calibration_ms; /* added to the audio position (rhythm_core.h timeline) */
    uint8_t  scroll_idx;     /* 0..BB_SCROLL_STEPS-1 */
    uint8_t  music_vol;      /* 0..10 */
    uint8_t  sfx_vol;        /* 0..10 */
    uint8_t  unlock_all;     /* 1 = every level selectable regardless of progression */
    uint8_t  master_vol;     /* 0..10 (v3+) scales every channel, like the original Audio > Master */
    uint8_t  voice_vol;      /* 0..10 (v3+) character voice lines */
    uint8_t  skip_cutscene;  /* 1 = skip the pre/post-level cutscenes (v4+), SettingsManager "skip_cutscene" */
    uint8_t  reduce_motion;  /* 1 = no menu slide-ins, shakes, sparkles or ambient particles (v5+), SettingsManager "reduce_motion" */
    uint8_t  scroll_mode;    /* bb_scroll_mode_t (v6+): Settings > Gameplay > Scroll Mode, like SettingsManager "scroll_mode_index" */
    uint8_t  theme;          /* bb_theme_t (v7+): the main menu's Change Theme button (Calendar.GAME_THEME DEFAULT / HALLOWEEN) */
} bb_settings_t;

/* Calendar.THEME values the port can show (the original's APRIL_FOOLS / CHRISTMAS have no assets) */
typedef enum { BB_THEME_DEFAULT = 0, BB_THEME_HALLOWEEN = 1, BB_THEME_COUNT = 2 } bb_theme_t;

/* Gameplay highway direction on the bottom screen; all three keep the four lane quadrants. */
typedef enum { BB_SCROLL_DOWN = 0, BB_SCROLL_UP = 1, BB_SCROLL_SIDE = 2, BB_SCROLL_MODE_COUNT = 3 } bb_scroll_mode_t;

typedef struct {
    char     id[BB_SAVE_ID_LEN];
    int32_t  best_score[3];
    uint8_t  best_grade[3];  /* bb_grade_t; 0 = never completed */
    uint16_t best_combo[3];
} bb_stage_save_t;

typedef struct {
    char     id[BB_SAVE_ID_LEN];
    uint8_t  levels_unlocked;
} bb_act_save_t;

/* Economy / collection state (v2): SaveManager.data["player"], unlocked_items, unlocked_achievements, stats */
typedef struct {
    int32_t  money;                    /* 0..99999 */
    int32_t  exp;                      /* 0..9999 */
    uint16_t equipped_powerup;         /* items.gd ITEM enum, 0 = none */
    uint16_t menu_music;               /* song item enum, 0 = default menu theme */
    uint16_t outfit;                   /* Jasmine outfit enum, 0 = default */
    uint16_t item_count;
    uint16_t items[BB_MAX_ITEMS];      /* owned/unlocked item enums (powerups, songs, photos, outfits, levels, collectables) */
    uint16_t ach_count;
    uint16_t achs[BB_MAX_ACHS];
    int32_t  stats[BB_STAT_COUNT];     /* index = STATS enum - 801 */
} bb_player_t;

enum { BB_STAT_LAUNCHED = 0, BB_STAT_LEVELS_BEATEN, BB_STAT_LEVELS_FAILED, BB_STAT_MODS_BEATEN, BB_STAT_MODS_FAILED,
       BB_STAT_BEST_COMBO, BB_STAT_HIT_NOTES, BB_STAT_MISSED_NOTES, BB_STAT_PERFECT_CLEARS, BB_STAT_NOSE_HONKS };

typedef struct {
    bb_settings_t   settings;
    bb_player_t     player;
    uint32_t        stage_count;
    bb_stage_save_t stages[BB_SAVE_MAX_STAGES];
    uint32_t        act_count;
    bb_act_save_t   acts[BB_SAVE_MAX_ACTS];
} bb_save_t;

void   bb_save_defaults(bb_save_t* s);
/* Serialize to out (cap bytes); returns the image size, 0 on failure. */
size_t bb_save_serialize(const bb_save_t* s, uint8_t* out, size_t cap);
/* Validate and load; returns false (and resets to defaults) on any inconsistency. */
bool   bb_save_deserialize(bb_save_t* s, const uint8_t* data, size_t size);

float  bb_scroll_multiplier(uint8_t scroll_idx);
uint32_t bb_crc32(const uint8_t* data, size_t n);

/* ---- player block helpers ---- */
bool bb_player_has_item(const bb_player_t* p, int item);
bool bb_player_add_item(bb_player_t* p, int item);      /* false if already owned or full */
bool bb_player_remove_item(bb_player_t* p, int item);
bool bb_player_has_ach(const bb_player_t* p, int ach);
bool bb_player_add_ach(bb_player_t* p, int ach);
void bb_player_stat_add(bb_player_t* p, int stat, int32_t amount);
void bb_player_stat_max(bb_player_t* p, int stat, int32_t value);

const bb_stage_save_t* bb_save_find_stage(const bb_save_t* s, const char* id);
/* Record a finished run; keeps the best of each field. Returns true if anything improved. */
bool   bb_save_record_result(bb_save_t* s, const char* stage_id, int diff, int32_t score, uint8_t grade, uint16_t combo);

uint8_t bb_save_levels_unlocked(const bb_save_t* s, const char* act_id);
/* Completing level_index unlocks level_index + 1 (save.gd unlock_level). */
void   bb_save_complete_level(bb_save_t* s, const char* act_id, uint8_t level_index);
/* Stage index rows (romfs:/stages/index.txt and the SD mods' index): id|title|character|act|act_name|act_index|level_index|level_enum
 * [|level_prereq]. Splits `line` in place on '|' into at most `max` fields (the last one keeps any further bars); returns the count. */
#define BB_INDEX_FIELDS 9
int    bb_index_split(char* line, char** fld, int max);
/* Room for one more record that is NOT part of the built-in game (an SD mod), after reserving a slot for every built-in stage and act
 * that has no record yet: with mods installed the tables can fill up, and a mod must never take the slot a built-in volume needs
 * to record its progress (that blocked the next level from unlocking). */
bool   bb_save_room_for_extra(const bb_save_t* s, const char* const* builtin_stages, int n_stages, const char* const* builtin_acts, int n_acts);
/* Level k of an act is selectable if unlock_all or k <= levels_unlocked (the first level is always open). */
bool   bb_save_level_unlocked(const bb_save_t* s, const char* act_id, uint8_t level_index);

#endif
