/**
 * @file test_save_input.c
 * @brief Host tests for core/save_data.c (settings + progression image) and core/input_map.h (control schemes).
 */
#include <stdio.h>
#include <string.h>
#include "save_data.h"
#include "input_map.h"

static int g_run = 0, g_failed = 0;
#define CHECK(cond, msg) do { g_run++; if (!(cond)) { g_failed++; fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); } } while (0)

static void test_input(void) {
    /* modes */
    CHECK(bb_input_lanes(BB_CTRL_BUTTONS, 0x3, 0xC) == 0x3, "buttons ignore dpad");
    CHECK(bb_input_lanes(BB_CTRL_DPAD, 0x3, 0xC) == 0xC, "dpad ignores buttons");
    CHECK(bb_input_lanes(BB_CTRL_HYBRID, 0x3, 0xC) == 0xF, "hybrid ORs both groups");
    CHECK(bb_input_lanes(BB_CTRL_HYBRID, 0xF3, 0) == 0x3, "mask limited to 4 lanes");
    CHECK(bb_input_lanes_ex(BB_CTRL_SHOULDERS, 0x3, 0xC, 0x5) == 0x5, "shoulders use only the L / ZL / ZR / R group");
    CHECK(bb_input_lanes_ex(BB_CTRL_BUTTONS, 0x3, 0xC, 0x5) == 0x3, "buttons ignore the shoulders");
    CHECK(bb_input_lanes(BB_CTRL_SHOULDERS, 0xF, 0xF) == 0x0, "the 2-group helper has no shoulder lanes");

    /* Old 3DS / 2DS: no ZL / ZR, so Shoulders is never offered or played there */
    CHECK(bb_input_mode_available(BB_CTRL_SHOULDERS, 1) && !bb_input_mode_available(BB_CTRL_SHOULDERS, 0), "shoulders need ZL / ZR");
    CHECK(bb_input_mode_available(BB_CTRL_HYBRID, 0) && !bb_input_mode_available(BB_CTRL_COUNT, 1), "other modes on every console, no mode past the table");
    CHECK(bb_input_mode_effective(BB_CTRL_SHOULDERS, 0) == BB_CTRL_BUTTONS, "a New 3DS save's Shoulders plays as Buttons on an Old 3DS");
    CHECK(bb_input_mode_effective(BB_CTRL_SHOULDERS, 1) == BB_CTRL_SHOULDERS && bb_input_mode_effective(BB_CTRL_DPAD, 0) == BB_CTRL_DPAD, "kept where playable");
    CHECK(bb_input_mode_step(BB_CTRL_HYBRID, 1, 1) == BB_CTRL_SHOULDERS && bb_input_mode_step(BB_CTRL_SHOULDERS, 1, 1) == BB_CTRL_BUTTONS, "New 3DS cycles all four");
    CHECK(bb_input_mode_step(BB_CTRL_HYBRID, 1, 0) == BB_CTRL_BUTTONS && bb_input_mode_step(BB_CTRL_BUTTONS, -1, 0) == BB_CTRL_HYBRID, "Old 3DS skips Shoulders both ways");
    CHECK(bb_input_mode_step(BB_CTRL_SHOULDERS, 1, 0) == BB_CTRL_DPAD, "stepping from a stored Shoulders on an Old 3DS starts at Buttons");

    /* equivalent inputs pressed together = one press; release only after both let go */
    uint8_t pr, re, prev = 0, cur;
    cur = bb_input_lanes(BB_CTRL_HYBRID, 0x1, 0x1); /* B and Down together */
    bb_input_edges(prev, cur, &pr, &re);
    CHECK(pr == 0x1 && re == 0, "B+Down together press lane 0 once");
    prev = cur;
    cur = bb_input_lanes(BB_CTRL_HYBRID, 0x0, 0x1); /* B released, Down still held */
    bb_input_edges(prev, cur, &pr, &re);
    CHECK(pr == 0 && re == 0, "hold continues while the other group is held");
    prev = cur;
    cur = bb_input_lanes(BB_CTRL_HYBRID, 0x0, 0x0);
    bb_input_edges(prev, cur, &pr, &re);
    CHECK(pr == 0 && re == 0x1, "release when both groups are up");
    /* staggered: Down first, then B while Down held -> no second press */
    prev = bb_input_lanes(BB_CTRL_HYBRID, 0x0, 0x1);
    cur = bb_input_lanes(BB_CTRL_HYBRID, 0x1, 0x1);
    bb_input_edges(prev, cur, &pr, &re);
    CHECK(pr == 0, "second group joining a held lane is not a new press");
    /* different lanes at once */
    bb_input_edges(0, bb_input_lanes(BB_CTRL_HYBRID, 0x2, 0x4), &pr, &re);
    CHECK(pr == 0x6, "different lanes both press");
    /* mode switch mid-hold only matters via masks */
    bb_input_edges(0, bb_input_lanes(BB_CTRL_DPAD, 0xF, 0x0), &pr, &re);
    CHECK(pr == 0, "face buttons dead in D-pad mode");
}

static void test_save(void) {
    bb_save_t a, b;
    uint8_t buf[BB_SAVE_MAX_BYTES];
    bb_save_defaults(&a);
    CHECK(a.settings.control_mode == BB_CTRL_BUTTONS && a.settings.scroll_idx == 0 && a.settings.unlock_all == 0, "defaults: a fresh save starts locked (natural progression)");
    a.settings.control_mode = BB_CTRL_HYBRID;
    a.settings.calibration_ms = -35;
    a.settings.scroll_idx = 3;
    a.settings.music_vol = 7;
    a.settings.sfx_vol = 4;
    a.settings.unlock_all = 0;
    CHECK(bb_save_record_result(&a, "cathy_e1", 0, 25844, 6, 288), "first result recorded");
    CHECK(!bb_save_record_result(&a, "cathy_e1", 0, 100, 2, 10), "worse result changes nothing");
    CHECK(bb_save_record_result(&a, "cathy_e1", 0, 30000, 7, 300), "better result recorded");
    CHECK(bb_save_record_result(&a, "zoe_e1", 2, -20, 1, 0), "negative first score still recorded");
    bb_save_complete_level(&a, "bb_vol_1", 0);
    bb_save_complete_level(&a, "bb_vol_1", 0);
    bb_save_complete_level(&a, "bb_vol_2", 2);
    CHECK(bb_save_levels_unlocked(&a, "bb_vol_1") == 1, "completing level 0 unlocks 1 (idempotent)");
    CHECK(bb_save_levels_unlocked(&a, "bb_vol_2") == 3, "completing level 2 unlocks 3");
    CHECK(bb_save_levels_unlocked(&a, "nope") == 0, "unknown act");
    CHECK(bb_save_level_unlocked(&a, "bb_vol_1", 1) && !bb_save_level_unlocked(&a, "bb_vol_1", 2), "progression gating");
    a.settings.unlock_all = 1;
    CHECK(bb_save_level_unlocked(&a, "bb_vol_1", 9), "unlock_all overrides");
    a.settings.unlock_all = 0;

    size_t n = bb_save_serialize(&a, buf, sizeof(buf));
    CHECK(n > 16 && n < BB_SAVE_MAX_BYTES, "serialized");
    CHECK(bb_save_deserialize(&b, buf, n), "roundtrip loads");
    CHECK(memcmp(&a.settings, &b.settings, sizeof(a.settings)) == 0, "settings roundtrip");
    const bb_stage_save_t* c = bb_save_find_stage(&b, "cathy_e1");
    CHECK(c && c->best_score[0] == 30000 && c->best_grade[0] == 7 && c->best_combo[0] == 300, "stage roundtrip");
    CHECK(bb_save_find_stage(&b, "zoe_e1")->best_score[2] == -20, "negative score roundtrip");
    CHECK(bb_save_levels_unlocked(&b, "bb_vol_2") == 3, "act roundtrip");

    /* corruption is rejected and yields defaults */
    uint8_t bad[BB_SAVE_MAX_BYTES];
    memcpy(bad, buf, n);
    bad[n - 1] ^= 0x55;
    CHECK(!bb_save_deserialize(&b, bad, n) && b.settings.control_mode == BB_CTRL_BUTTONS && b.stage_count == 0, "bad crc -> defaults");
    memcpy(bad, buf, n);
    bad[4] = 9;
    CHECK(!bb_save_deserialize(&b, bad, n), "unknown version rejected");
    CHECK(!bb_save_deserialize(&b, buf, n - 3), "truncated rejected");
    CHECK(!bb_save_deserialize(&b, buf, 5), "tiny rejected");
    CHECK(!bb_save_deserialize(&b, NULL, 0), "null rejected");
    /* out-of-range settings with a VALID crc are still rejected */
    bb_save_t evil = a;
    evil.settings.scroll_idx = 99;
    size_t m = bb_save_serialize(&evil, buf, sizeof(buf));
    CHECK(!bb_save_deserialize(&b, buf, m), "out-of-range scroll rejected despite valid crc");
    evil = a;
    evil.settings.calibration_ms = 500;
    m = bb_save_serialize(&evil, buf, sizeof(buf));
    CHECK(!bb_save_deserialize(&b, buf, m), "out-of-range calibration rejected");
    /* capacity */
    CHECK(bb_save_serialize(&a, buf, 20) == 0, "too-small buffer fails");
    /* scroll table */
    CHECK(bb_scroll_multiplier(0) == 1.0f && bb_scroll_multiplier(6) == 6.0f && bb_scroll_multiplier(200) == 1.0f, "scroll multipliers");
    /* crc known vector */
    CHECK(bb_crc32((const uint8_t*)"123456789", 9) == 0xCBF43926u, "crc32 check value");
    /* many stages */
    bb_save_defaults(&a);
    char id[16];
    for (int i = 0; i < BB_SAVE_MAX_STAGES; i++) { snprintf(id, sizeof(id), "s%d", i); CHECK(bb_save_record_result(&a, id, 1, i, 3, 1), "fill stage table"); }
    CHECK(!bb_save_record_result(&a, "overflow", 1, 1, 3, 1), "stage table full is safe");
    n = bb_save_serialize(&a, buf, sizeof(buf));
    CHECK(n > 0 && bb_save_deserialize(&b, buf, n) && b.stage_count == BB_SAVE_MAX_STAGES, "full table roundtrip");
}

int main(void) {
    printf("=== Beat Banger save-data & input-map host tests ===\n");
    test_input();
    test_save();
    printf("Executed %d assertions: %d failed.\n", g_run, g_failed);
    if (g_failed) { printf("[FAIL]\n"); return 1; }
    printf("[PASS] save/input tests succeeded.\n");
    return 0;
}
