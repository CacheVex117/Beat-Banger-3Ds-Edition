/**
 * @file test_rhythm_core.c
 * @brief Host deterministic verification suite for native3ds/core/ (no <3ds.h>).
 *
 * Rules under test come from the current game source (see rhythm_core.h):
 *   judgment windows, absent presses, late misses + forgiveness, health, ghost notes,
 *   hold notes, grade, the single chart timeline, events, and full playthroughs of
 *   the converted Cathy E1 charts (generated/ must exist: run tools/convert_stage.py).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rhythm_core.h"
#include "chart_loader.h"

static int g_checks_run = 0;
static int g_checks_failed = 0;

#define EXPECT_EQ(actual, expected, msg) do { \
    g_checks_run++; \
    long long _a = (long long)(actual); \
    long long _e = (long long)(expected); \
    if (_a != _e) { \
        g_checks_failed++; \
        fprintf(stderr, "[FAIL] %s:%d: %s (got %lld, expected %lld)\n", __FILE__, __LINE__, (msg), _a, _e); \
    } \
} while (0)

#define EXPECT_TRUE(cond, msg) do { \
    g_checks_run++; \
    if (!(cond)) { \
        g_checks_failed++; \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
    } \
} while (0)

#define SONG_OFFSET 1000000LL

static bb_rhythm_core_t g_core;
static bb_chart_t g_chart;

/* Audio position that corresponds to chart time t (calibration 0). */
static int64_t pos_at(int64_t chart_us) { return chart_us + SONG_OFFSET; }

static void add_note(uint32_t idx, int64_t ts, uint8_t lane, uint8_t mod, int64_t hold_len) {
    g_chart.notes[idx].timestamp_us = ts;
    g_chart.notes[idx].input_type = lane;
    g_chart.notes[idx].modifier = mod;
    g_chart.notes[idx].active = 1;
    g_chart.notes[idx].hold_end_us = (mod == BB_MOD_HOLD) ? ts + hold_len : 0;
}

static void begin(uint32_t count) {
    g_chart.note_count = count;
    bb_core_init(&g_core, &g_chart);
    bb_core_start(&g_core, 0);
}

static void new_chart(void) {
    memset(&g_chart, 0, sizeof(g_chart));
    strcpy(g_chart.stage_id, "unit_test");
    g_chart.initial_bpm_milli = 120000;
    g_chart.song_offset_us = SONG_OFFSET;
}

static int count_events(bb_event_type_t type) {
    int n = 0;
    bb_event_t e;
    while (bb_core_poll_event(&g_core, &e)) {
        if (e.type == type) n++;
    }
    return n;
}

/* 1. Windows: PERFECT <60, GREAT <75, NICE <112.5, MEH <=150, else absent */
static void test_judgment_windows(void) {
    struct { int64_t off; bb_judgment_t want; } cases[] = {
        {0, BB_JUDGE_PERFECT}, {59999, BB_JUDGE_PERFECT}, {-59999, BB_JUDGE_PERFECT},
        {60000, BB_JUDGE_GREAT}, {74999, BB_JUDGE_GREAT},
        {75000, BB_JUDGE_NICE}, {112499, BB_JUDGE_NICE},
        {112500, BB_JUDGE_MEH}, {150000, BB_JUDGE_MEH}, {-150000, BB_JUDGE_MEH},
        {150001, BB_JUDGE_ABSENT}, {-150001, BB_JUDGE_ABSENT},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        new_chart();
        add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
        begin(1);
        bb_judgment_t j = bb_core_press(&g_core, BB_INPUT_A, pos_at(2000000 + cases[i].off));
        EXPECT_EQ(j, cases[i].want, "window boundary judgment");
    }
}

/* 2. Absent press: -20, combo reset, registered miss, no health loss, note not consumed */
static void test_absent_press(void) {
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    add_note(1, 3000000, BB_INPUT_B, BB_MOD_NORMAL, 0);
    begin(2);
    EXPECT_EQ(bb_core_press(&g_core, BB_INPUT_A, pos_at(2000000)), BB_JUDGE_PERFECT, "first hit");
    EXPECT_EQ(g_core.stats.combo, 1, "combo 1");
    int32_t score = g_core.stats.score;
    /* Wrong lane near note 1: B note is at 3.0s; pressing A at 3.0s has no lane-A note left */
    EXPECT_EQ(bb_core_press(&g_core, BB_INPUT_A, pos_at(3000000)), BB_JUDGE_ABSENT, "wrong lane is absent");
    EXPECT_EQ(g_core.stats.score, score - 20, "absent costs 20 points");
    EXPECT_EQ(g_core.stats.combo, 0, "absent resets combo");
    EXPECT_EQ(g_core.stats.health, 4, "absent does not damage health");
    EXPECT_EQ(g_core.stats.registered, 2, "absent is a registered miss");
    EXPECT_EQ(g_core.stats.hit, 1, "only one hit");
    /* The B note is still there and hittable */
    EXPECT_EQ(bb_core_press(&g_core, BB_INPUT_B, pos_at(3000000)), BB_JUDGE_PERFECT, "note not consumed by wrong press");
    /* Too-early press in the right lane is absent as well and the note survives */
    new_chart();
    add_note(0, 5000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(1);
    EXPECT_EQ(bb_core_press(&g_core, BB_INPUT_A, pos_at(4000000)), BB_JUDGE_ABSENT, "too early is absent");
    EXPECT_EQ(g_core.chart.notes[0].active, 1, "early press leaves note active");
    /* Score may go negative (score_display.gd has no clamp) */
    EXPECT_EQ(g_core.stats.score, -20, "score -20");
}

/* 3. Late misses, health, failure, forgiveness */
static void test_late_miss_health_and_forgiveness(void) {
    new_chart();
    for (uint32_t i = 0; i < 6; i++) add_note(i, 2000000 + (int64_t)i * 1000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(6);
    bb_core_step(&g_core, pos_at(2200000));
    EXPECT_EQ(g_core.stats.missed, 0, "200 ms exactly is not yet late");
    bb_core_step(&g_core, pos_at(2200001));
    EXPECT_EQ(g_core.stats.missed, 1, "late after 200 ms");
    EXPECT_EQ(g_core.stats.health, 3, "miss costs 1 health");
    EXPECT_EQ(g_core.stats.registered, 1, "late miss registered");
    /* misses at 3.2, 4.2, 5.2, 6.2 -> health 2,1,0 then fail on the 5th counted miss */
    bb_core_step(&g_core, pos_at(3300000));
    bb_core_step(&g_core, pos_at(4300000));
    bb_core_step(&g_core, pos_at(5300000));
    EXPECT_EQ(g_core.stats.health, 0, "health 0 still alive");
    EXPECT_EQ(g_core.state, BB_STATE_PLAYING, "alive at 0 health");
    bb_core_step(&g_core, pos_at(6300000));
    EXPECT_EQ(g_core.state, BB_STATE_FAILED, "5th miss fails (health < 0)");

    /* Forgiveness: two notes expiring within 100 ms are one counted miss */
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    add_note(1, 2050000, BB_INPUT_B, BB_MOD_NORMAL, 0);
    add_note(2, 2400000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(3);
    bb_core_step(&g_core, pos_at(3000000));
    EXPECT_EQ(g_core.stats.passed_notes, 3, "all three expired");
    EXPECT_EQ(g_core.stats.missed, 2, "2nd forgiven (50 ms), 3rd counted (>=100 ms after first)");
    EXPECT_EQ(g_core.stats.health, 2, "two counted misses");
    EXPECT_EQ(g_core.stats.registered, 2, "forgiven miss is not registered");

    /* no_fail: never loses health or fails; hardcore: first miss fails */
    new_chart();
    for (uint32_t i = 0; i < 8; i++) add_note(i, 2000000 + (int64_t)i * 1000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(8);
    g_core.no_fail = true;
    bb_core_step(&g_core, pos_at(9250000));
    EXPECT_EQ(g_core.state, BB_STATE_PLAYING, "no_fail survives");
    EXPECT_EQ(g_core.stats.health, 4, "no_fail keeps health");
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    add_note(1, 9000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(2);
    g_core.hardcore = true;
    bb_core_step(&g_core, pos_at(2300000));
    EXPECT_EQ(g_core.state, BB_STATE_FAILED, "hardcore fails on first miss");
}

/* 4. Perfect regeneration; ghost notes do not regenerate and are flagged */
static void test_regen_and_ghost(void) {
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    add_note(1, 3000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    add_note(2, 4000000, BB_INPUT_A, BB_MOD_GHOST, 0);
    add_note(3, 5000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(4);
    bb_core_step(&g_core, pos_at(2300000)); /* miss note 0 -> health 3 */
    EXPECT_EQ(g_core.stats.health, 3, "health 3 after miss");
    count_events(BB_EVT_NONE);
    EXPECT_EQ(bb_core_press(&g_core, BB_INPUT_A, pos_at(3000000)), BB_JUDGE_PERFECT, "perfect");
    EXPECT_EQ(g_core.stats.health, 4, "PERFECT regenerates");
    g_core.stats.health = 2;
    EXPECT_EQ(bb_core_press(&g_core, BB_INPUT_A, pos_at(4000000)), BB_JUDGE_PERFECT, "ghost perfect");
    EXPECT_EQ(g_core.stats.health, 2, "ghost perfect does not regenerate");
    bb_event_t e;
    int ghost_flagged = 0;
    while (bb_core_poll_event(&g_core, &e)) {
        if (e.type == BB_EVT_HIT && e.note_idx == 2 && (e.flags & BB_EVTF_IGNORE_ANIM)) ghost_flagged = 1;
    }
    EXPECT_TRUE(ghost_flagged, "ghost hit event carries ignore-animation");
    EXPECT_EQ(g_core.stats.score, 100 + 100, "score of two perfects");
}

/* 5. Hold notes */
static void test_holds(void) {
    /* Held through the tail: start hit + automatic tail hit, ~100 pts/s */
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_HOLD, 1000000);
    begin(1);
    EXPECT_EQ(bb_core_press(&g_core, BB_INPUT_A, pos_at(2000000)), BB_JUDGE_PERFECT, "hold start perfect");
    EXPECT_EQ(g_core.stats.registered, 1, "hold start registered");
    EXPECT_EQ(g_core.chart.notes[0].active, 1, "hold stays active");
    for (int64_t t = 2000000; t <= 3000000; t += 16667) bb_core_step(&g_core, pos_at(t));
    EXPECT_EQ(g_core.stats.registered, 1, "tail not reached yet");
    bb_core_step(&g_core, pos_at(3000001));
    EXPECT_EQ(g_core.stats.registered, 2, "tail completion registers a second hit");
    EXPECT_EQ(g_core.stats.hit, 2, "two hits");
    EXPECT_EQ(g_core.chart.notes[0].active, 0, "hold resolved");
    /* 100 (start) + 100 (tail PERFECT) + ~100 held for 1 s */
    EXPECT_TRUE(g_core.stats.score >= 295 && g_core.stats.score <= 300, "hold accrues ~100 pts/s");
    EXPECT_EQ(g_core.stats.health, 4, "no regen overflow");

    /* Early release: graded by distance to tail, never a miss */
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_HOLD, 2000000);
    begin(1);
    bb_core_press(&g_core, BB_INPUT_A, pos_at(2000000));
    g_core.stats.health = 2;
    bb_judgment_t j = bb_core_release(&g_core, BB_INPUT_A, pos_at(2500000));
    EXPECT_EQ(j, BB_JUDGE_MEH, "very early release = MEH");
    EXPECT_EQ(g_core.stats.missed, 0, "release is never a miss");
    EXPECT_EQ(g_core.stats.health, 2, "hold end never regenerates health");
    EXPECT_EQ(g_core.stats.registered, 2, "start + release");

    /* Release near the tail */
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_HOLD, 1000000);
    begin(1);
    bb_core_press(&g_core, BB_INPUT_A, pos_at(2000000));
    EXPECT_EQ(bb_core_release(&g_core, BB_INPUT_A, pos_at(2950000)), BB_JUDGE_PERFECT, "release 50 ms early = PERFECT");

    /* Release with nothing held is ignored */
    EXPECT_EQ(bb_core_release(&g_core, BB_INPUT_B, pos_at(3000000)), BB_JUDGE_NONE, "release without hold");

    /* Hold never pressed -> late miss, counted once */
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_HOLD, 1000000);
    begin(1);
    bb_core_step(&g_core, pos_at(2300000));
    EXPECT_EQ(g_core.stats.missed, 1, "unpressed hold misses");

    /* A held note does not expire as 'late' while later notes continue */
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_HOLD, 3000000);
    add_note(1, 2500000, BB_INPUT_B, BB_MOD_NORMAL, 0);
    begin(2);
    bb_core_press(&g_core, BB_INPUT_A, pos_at(2000000));
    EXPECT_EQ(bb_core_press(&g_core, BB_INPUT_B, pos_at(2500000)), BB_JUDGE_PERFECT, "tap during hold");
    bb_core_step(&g_core, pos_at(4000000));
    EXPECT_EQ(g_core.stats.missed, 0, "held note is not late");
    EXPECT_EQ(g_core.chart.notes[0].holding, 1, "still holding at 4 s");
}

/* 5b. Power-ups: score multiplier, damage, extra hearts, hardcore on absent */
static void test_powerups(void) {
    /* 0.5x score, truncated per hit: PERFECT 100 -> 50, GREAT 50 -> 25, NICE 10 -> 5, MEH 5 -> 2 */
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    add_note(1, 3000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    add_note(2, 4000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    add_note(3, 5000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    g_chart.note_count = 4;
    bb_core_init(&g_core, &g_chart);
    g_core.score_mult_x10 = 5;
    bb_core_restart(&g_core);
    bb_core_start(&g_core, 0);
    bb_core_press(&g_core, BB_INPUT_A, pos_at(2000000));
    bb_core_press(&g_core, BB_INPUT_A, pos_at(3000000 + 70000));
    bb_core_press(&g_core, BB_INPUT_A, pos_at(4000000 + 100000));
    bb_core_press(&g_core, BB_INPUT_A, pos_at(5000000 + 140000));
    EXPECT_EQ(g_core.stats.score, 50 + 25 + 5 + 2, "0.5x score truncates per hit");

    /* 4x */
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(1);
    g_core.score_mult_x10 = 40;
    bb_core_restart(&g_core);
    bb_core_start(&g_core, 0);
    bb_core_press(&g_core, BB_INPUT_A, pos_at(2000000));
    EXPECT_EQ(g_core.stats.score, 400, "4x score");

    /* Performance Pills: 7 hearts, fails on the 8th counted miss */
    new_chart();
    for (uint32_t i = 0; i < 10; i++) add_note(i, 2000000 + (int64_t)i * 1000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    g_chart.note_count = 10;
    bb_core_init(&g_core, &g_chart);
    g_core.extra_health = 3;
    bb_core_restart(&g_core);
    bb_core_start(&g_core, 0);
    EXPECT_EQ(g_core.stats.max_health, 7, "extra lives raise max health");
    EXPECT_EQ(g_core.stats.health, 7, "start at full health");
    bb_core_step(&g_core, pos_at(8300000));
    EXPECT_EQ(g_core.state, BB_STATE_PLAYING, "still alive after 7 misses (health 0)");
    EXPECT_EQ(g_core.stats.health, 0, "7 misses -> 0 hearts");
    bb_core_step(&g_core, pos_at(9300000));
    EXPECT_EQ(g_core.state, BB_STATE_FAILED, "8th miss fails");

    /* Double Edged Sword: damage 2 -> 4 hearts survive 2 misses, fail on the 3rd */
    new_chart();
    for (uint32_t i = 0; i < 6; i++) add_note(i, 2000000 + (int64_t)i * 1000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    g_chart.note_count = 6;
    bb_core_init(&g_core, &g_chart);
    g_core.damage = 2;
    bb_core_restart(&g_core);
    bb_core_start(&g_core, 0);
    bb_core_step(&g_core, pos_at(3300000));
    EXPECT_EQ(g_core.stats.health, 0, "two misses at damage 2 -> 0 hearts");
    EXPECT_EQ(g_core.state, BB_STATE_PLAYING, "alive at 0");
    bb_core_step(&g_core, pos_at(4300000));
    EXPECT_EQ(g_core.state, BB_STATE_FAILED, "third miss fails");

    /* Hardcore Lube: an absent press fails immediately */
    new_chart();
    add_note(0, 5000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(1);
    g_core.hardcore = true;
    EXPECT_EQ(bb_core_press(&g_core, BB_INPUT_A, pos_at(2000000)), BB_JUDGE_ABSENT, "absent press");
    EXPECT_EQ(g_core.state, BB_STATE_FAILED, "hardcore fails on absent");

    /* defaults restored by init */
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(1);
    EXPECT_EQ(g_core.score_mult_x10, 10, "default multiplier");
    EXPECT_EQ(g_core.damage, 1, "default damage");
    EXPECT_EQ(g_core.stats.max_health, 4, "default hearts");
}

/* 6. Grades */
static void test_grades(void) {
    bb_stats_t s;
    memset(&s, 0, sizeof(s));
    EXPECT_EQ(bb_core_calculate_grade(&s), BB_GRADE_NONE, "no notes -> none");
    struct { uint32_t hit, reg, meh, nice; bb_grade_t want; } c[] = {
        {59, 100, 0, 0, BB_GRADE_F}, {60, 100, 0, 0, BB_GRADE_D}, {69, 100, 0, 0, BB_GRADE_D},
        {70, 100, 0, 0, BB_GRADE_C}, {79, 100, 0, 0, BB_GRADE_C}, {80, 100, 0, 0, BB_GRADE_B},
        {89, 100, 0, 0, BB_GRADE_B}, {90, 100, 0, 0, BB_GRADE_A}, {99, 100, 0, 0, BB_GRADE_A},
        {100, 100, 0, 0, BB_GRADE_SS}, {100, 100, 0, 3, BB_GRADE_S}, {100, 100, 2, 0, BB_GRADE_S},
    };
    for (size_t i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
        memset(&s, 0, sizeof(s));
        s.hit = c[i].hit; s.registered = c[i].reg; s.meh = c[i].meh; s.nice = c[i].nice;
        EXPECT_EQ(bb_core_calculate_grade(&s), c[i].want, "grade threshold");
    }
}

/* 7. Single timeline: chart = pos + calibration - song_offset; no double offsets */
static void test_timeline_and_calibration(void) {
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(1);
    bb_core_step(&g_core, 3000000);
    EXPECT_EQ(g_core.song_cur_time_us, 2000000, "chart time = audio pos - song_offset");
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(1);
    g_core.calibration_offset_us = 20000; /* audio clock reads 20 ms late -> chart time shifts +20 ms */
    bb_core_step(&g_core, 3000000);
    EXPECT_EQ(g_core.song_cur_time_us, 2020000, "calibration is added to audio position");
    /* note_offset is stored but not applied (the game never reads it) */
    new_chart();
    g_chart.note_offset_us = 500000;
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(1);
    bb_core_step(&g_core, 3000000);
    EXPECT_EQ(g_core.song_cur_time_us, 2000000, "note_offset ignored");
    /* Monotonic */
    bb_core_step(&g_core, 2500000);
    EXPECT_EQ(g_core.song_cur_time_us, 2000000, "clock never runs backwards");
}

/* 8. Events, loops, shutters, time-til-next, tempo integration */
static void test_events_and_tempo(void) {
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    add_note(1, 2000000, BB_INPUT_B, BB_MOD_NORMAL, 0);
    add_note(2, 2400000, BB_INPUT_A, BB_MOD_GHOST, 0);
    add_note(3, 3000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    g_chart.shutter_count = 1;
    g_chart.shutter_times_us[0] = 2500000;
    g_chart.loop_count = 2;
    g_chart.loops[0].timestamp_us = 0;
    g_chart.loops[1].timestamp_us = 2600000;
    g_chart.bpm_change_count = 2;
    g_chart.bpm_changes[0].timestamp_us = 0;       g_chart.bpm_changes[0].bpm_milli = 120000;
    g_chart.bpm_changes[1].timestamp_us = 2000000; g_chart.bpm_changes[1].bpm_milli = 60000;
    begin(4);
    EXPECT_EQ(bb_core_time_til_next_note_us(&g_core, 0), 1000000, "skips same-timestamp and ghost");
    EXPECT_EQ(bb_core_time_til_next_note_us(&g_core, 3), 0, "last note has no successor");
    bb_core_step(&g_core, pos_at(2000000));
    EXPECT_EQ(g_core.current_beat, 4, "2 s at 120 BPM = 4 beats");
    bb_core_step(&g_core, pos_at(4000000));
    EXPECT_EQ(g_core.current_beat, 6, "then 2 s at 60 BPM = 2 more beats (piecewise)");
    EXPECT_EQ(count_events(BB_EVT_SHUTTER), 1, "shutter event");
    new_chart();
    g_chart.loop_count = 2;
    g_chart.loops[1].timestamp_us = 1000000;
    add_note(0, 5000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(1);
    bb_core_step(&g_core, pos_at(1500000));
    EXPECT_EQ(count_events(BB_EVT_LOOP), 1, "loop event");
    EXPECT_EQ(g_core.current_loop_idx, 1, "loop index");
}

/* 9. Pause / resume / restart, frame-rate independence */
static void run_stream(int64_t frame_us, bb_rhythm_core_t* out) {
    new_chart();
    for (uint32_t i = 0; i < 20; i++) add_note(i, 2000000 + (int64_t)i * 500000, (uint8_t)(i % 2), BB_MOD_NORMAL, 0);
    begin(20);
    int64_t t = 0;
    uint32_t next = 0;
    while (g_core.state == BB_STATE_PLAYING && t < 20000000) {
        t += frame_us;
        bb_core_step(&g_core, t);
        /* Deterministic input stream: tap every third note 30 ms late, skip the others */
        while (next < 20 && pos_at(g_core.chart.notes[next].timestamp_us + 30000) <= t) {
            if (next % 3 == 0) bb_core_press(&g_core, g_core.chart.notes[next].input_type,
                                              pos_at(g_core.chart.notes[next].timestamp_us + 30000));
            next++;
        }
    }
    *out = g_core;
}

static void test_lifecycle_and_framerate(void) {
    new_chart();
    add_note(0, 2000000, BB_INPUT_A, BB_MOD_NORMAL, 0);
    begin(1);
    bb_core_step(&g_core, pos_at(1000000));
    bb_core_pause(&g_core);
    EXPECT_EQ(g_core.state, BB_STATE_PAUSED, "paused");
    bb_core_step(&g_core, pos_at(9000000));
    EXPECT_EQ(g_core.stats.missed, 0, "no progress while paused");
    EXPECT_EQ(bb_core_press(&g_core, BB_INPUT_A, pos_at(2000000)), BB_JUDGE_NONE, "ignored while paused");
    bb_core_resume(&g_core);
    EXPECT_EQ(bb_core_press(&g_core, BB_INPUT_A, pos_at(2000000)), BB_JUDGE_PERFECT, "works after resume");
    bb_core_restart(&g_core);
    EXPECT_EQ(g_core.state, BB_STATE_IDLE, "restart -> idle");
    EXPECT_EQ(g_core.stats.score, 0, "restart clears stats");
    EXPECT_EQ(g_core.chart.notes[0].active, 1, "restart reactivates notes");

    static bb_rhythm_core_t a, b, c;
    run_stream(16667, &a);
    run_stream(33333, &b);
    run_stream(7000, &c);
    EXPECT_EQ(a.stats.score, b.stats.score, "60 vs 30 FPS score");
    EXPECT_EQ(a.stats.score, c.stats.score, "60 vs 143 FPS score");
    EXPECT_EQ(a.stats.missed, b.stats.missed, "missed identical");
    EXPECT_EQ(a.stats.registered, c.stats.registered, "registered identical");
    EXPECT_EQ(a.state, b.state, "final state identical");
}

/* 10. Loader validation */
static void test_loader_validation(void) {
    uint8_t buf[152 + 16];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, "BBCH", 4);
    buf[4] = 1;
    buf[116] = 0xC0; buf[117] = 0xD4; buf[118] = 0x01; /* 120000 */
    buf[136] = 1;                                       /* one note */
    buf[152 + 9] = BB_MOD_HOLD;                         /* hold with zero duration */
    bb_chart_t* ch = (bb_chart_t*)malloc(sizeof(bb_chart_t));
    EXPECT_EQ(bb_chart_load_from_memory(ch, buf, sizeof(buf)), BB_LOAD_ERR_BAD_HOLD, "hold needs duration");
    EXPECT_EQ(ch->note_count, 0, "failed load leaves a zeroed chart");
    buf[152 + 11] = 0x10; /* duration 16 us */
    EXPECT_EQ(bb_chart_load_from_memory(ch, buf, sizeof(buf)), BB_LOAD_OK, "valid hold");
    EXPECT_EQ(ch->notes[0].hold_end_us, 16, "hold end = ts + duration");
    buf[152 + 9] = BB_MOD_NORMAL;
    EXPECT_EQ(bb_chart_load_from_memory(ch, buf, sizeof(buf)), BB_LOAD_ERR_BAD_HOLD, "duration on non-hold rejected");
    free(ch);
}

/* 11. Real converted Cathy E1 charts: structure + autoplay playthrough */
static int load_cathy(const char* diff, bb_chart_t* out) {
    char path[256];
    const char* prefixes[] = {"generated/", "../generated/", "../../generated/"};
    for (size_t i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "%scathy_e1_%s.bbchart", prefixes[i], diff);
        FILE* f = fopen(path, "rb");
        if (f) {
            fclose(f);
            return bb_chart_load_from_file(out, path) == BB_LOAD_OK;
        }
    }
    return 0;
}

static void test_cathy_e1(void) {
    const char* names[3] = {"virgin", "normal", "expert"};
    uint32_t notes[3] = {279, 304, 354};
    uint32_t holds[3] = {9, 33, 215};
    uint32_t ghosts[3] = {0, 0, 19};
    bb_chart_t* ch = (bb_chart_t*)malloc(sizeof(bb_chart_t));
    for (int d = 0; d < 3; d++) {
        if (!load_cathy(names[d], ch)) {
            fprintf(stderr, "[SKIP] generated/cathy_e1_%s.bbchart not found\n", names[d]);
            continue;
        }
        EXPECT_EQ(ch->note_count, notes[d], "note count matches source notes.cfg");
        EXPECT_EQ(ch->song_offset_us, 1900000, "song offset 1.9 s");
        EXPECT_EQ(ch->shutter_count, 5, "5 shutters");
        EXPECT_EQ(ch->loop_count, 5, "5 animation loops");
        uint32_t h = 0, g = 0;
        for (uint32_t i = 0; i < ch->note_count; i++) {
            if (ch->notes[i].modifier == BB_MOD_HOLD) { h++; EXPECT_TRUE(ch->notes[i].hold_end_us > ch->notes[i].timestamp_us, "hold tail after head"); }
            if (ch->notes[i].modifier == BB_MOD_GHOST) g++;
        }
        EXPECT_EQ(h, holds[d], "hold count matches source");
        EXPECT_EQ(g, ghosts[d], "ghost count matches source");

        /* Autoplay playthrough at 60 FPS to completion: every note hit PERFECT, SS grade */
        bb_core_init(&g_core, ch);
        g_core.autoplay = true;
        bb_core_restart(&g_core);
        bb_core_start(&g_core, 0);
        int64_t t = 0;
        int shutters = 0, loops = 0;
        bb_event_t e;
        while (g_core.state == BB_STATE_PLAYING && t < 200000000LL) {
            t += 16667;
            bb_core_step(&g_core, t);
            while (bb_core_poll_event(&g_core, &e)) {
                if (e.type == BB_EVT_SHUTTER) shutters++;
                if (e.type == BB_EVT_LOOP) loops++;
            }
        }
        EXPECT_EQ(g_core.state, BB_STATE_COMPLETED, "autoplay completes the stage");
        EXPECT_EQ(g_core.stats.registered, notes[d] + holds[d], "each hold registers two hits");
        EXPECT_EQ(g_core.stats.missed, 0, "no misses");
        EXPECT_EQ(g_core.stats.grade, BB_GRADE_SS, "perfect autoplay is SS");
        EXPECT_EQ(shutters, 5, "all shutters dispatched");
        EXPECT_EQ(loops, 4, "all loop switches dispatched");
        /* Completion at last note (152.381 s) + 0.3 s */
        EXPECT_TRUE(g_core.song_cur_time_us >= 152681000LL && g_core.song_cur_time_us < 152781000LL, "completes ~0.3 s after last note");

        /* Idle (no input) run fails */
        bb_core_init(&g_core, ch);
        bb_core_start(&g_core, 0);
        t = 0;
        while (g_core.state == BB_STATE_PLAYING && t < 200000000LL) { t += 16667; bb_core_step(&g_core, t); }
        EXPECT_EQ(g_core.state, BB_STATE_FAILED, "no input fails");
    }
    free(ch);
}

int main(void) {
    printf("=== Beat Banger Native 3DS Host Rhythm Core Test Suite ===\n");
    test_judgment_windows();
    test_absent_press();
    test_late_miss_health_and_forgiveness();
    test_regen_and_ghost();
    test_holds();
    test_powerups();
    test_grades();
    test_timeline_and_calibration();
    test_events_and_tempo();
    test_lifecycle_and_framerate();
    test_loader_validation();
    test_cathy_e1();
    printf("Executed %d assertions: %d failed.\n", g_checks_run, g_checks_failed);
    if (g_checks_failed) {
        printf("[FAIL] host rhythm-core tests failed.\n");
        return 1;
    }
    printf("[PASS] All host rhythm-core deterministic tests succeeded.\n");
    return 0;
}
