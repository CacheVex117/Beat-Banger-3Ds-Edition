/**
 * @file rhythm_core.c
 * @brief Platform-neutral Beat Banger rhythm & judgment engine implementation.
 *
 * Reference behaviour: see rhythm_core.h (C:\Beat Banger source).
 */
#include "rhythm_core.h"
#include <string.h>

static int64_t bb_abs64(int64_t v) {
    return (v < 0) ? -v : v;
}

static void bb_push_event(bb_rhythm_core_t* core, bb_event_type_t type, uint32_t note_idx,
                          bb_judgment_t judgment, uint8_t flags, uint8_t input_type) {
    if (core->evt_count == BB_EVENT_QUEUE_CAP) {
        /* Drop the oldest event */
        core->evt_head = (core->evt_head + 1) % BB_EVENT_QUEUE_CAP;
        core->evt_count--;
    }
    uint32_t slot = (core->evt_head + core->evt_count) % BB_EVENT_QUEUE_CAP;
    bb_event_t* e = &core->events[slot];
    e->type = type;
    e->note_idx = note_idx;
    e->judgment = judgment;
    e->flags = flags;
    e->input_type = input_type;
    e->chart_time_us = core->song_cur_time_us;
    core->evt_count++;
}

bool bb_core_poll_event(bb_rhythm_core_t* core, bb_event_t* out) {
    if (!core || !out || core->evt_count == 0) {
        return false;
    }
    *out = core->events[core->evt_head];
    core->evt_head = (core->evt_head + 1) % BB_EVENT_QUEUE_CAP;
    core->evt_count--;
    return true;
}

static void bb_advance_first_active_note(bb_rhythm_core_t* core) {
    while (core->first_active_note_idx < core->chart.note_count &&
           !core->chart.notes[core->first_active_note_idx].active) {
        core->first_active_note_idx++;
    }
}

static void bb_update_clock(bb_rhythm_core_t* core, int64_t raw_audio_pos_us) {
    /* Monotonic audio clock (game_music.gd smoothing never runs backwards in practice) */
    if (!core->clock_started) {
        core->song_pos_us = raw_audio_pos_us;
        core->clock_started = true;
    } else if (raw_audio_pos_us > core->song_pos_us) {
        core->song_pos_us = raw_audio_pos_us;
    }

    core->prev_cur_time_us = core->song_cur_time_us;
    /* The single gameplay timeline (see rhythm_core.h) */
    core->song_cur_time_us =
        core->song_pos_us + core->calibration_offset_us - core->chart.song_offset_us;

    /* Piecewise tempo integration: beats = sum(dt_segment * bpm_segment / 60) */
    int64_t t = core->song_cur_time_us;
    uint32_t cur_bpm = core->chart.initial_bpm_milli ? core->chart.initial_bpm_milli : 120000;
    if (t >= 0) {
        int64_t seg_start = 0;
        int64_t acc = 0; /* microsecond * bpm_milli */
        for (uint32_t i = 0; i < core->chart.bpm_change_count; i++) {
            int64_t cts = core->chart.bpm_changes[i].timestamp_us;
            if (cts > t) {
                break;
            }
            if (cts > seg_start) {
                acc += (cts - seg_start) * (int64_t)cur_bpm;
                seg_start = cts;
            }
            cur_bpm = core->chart.bpm_changes[i].bpm_milli ? core->chart.bpm_changes[i].bpm_milli : cur_bpm;
        }
        acc += (t - seg_start) * (int64_t)cur_bpm;
        core->current_beat = (int32_t)(acc / 60000000000LL);
    } else {
        core->current_beat = -1;
    }
    core->current_bpm_milli = cur_bpm;
}

static bb_judgment_t bb_grade_distance(int64_t abs_dist_us) {
    if (abs_dist_us < BB_PERFECT_WINDOW_US) return BB_JUDGE_PERFECT;
    if (abs_dist_us < BB_GREAT_WINDOW_US)   return BB_JUDGE_GREAT;
    if (abs_dist_us < BB_NICE_WINDOW_US)    return BB_JUDGE_NICE;
    return BB_JUDGE_MEH;
}

/* NoteDetector.note_hit: score, combo, registration, regen, event. */
static void bb_register_hit(bb_rhythm_core_t* core, uint32_t idx, bb_judgment_t j,
                            int64_t signed_delta_us, bool ignore_anim, uint8_t evt_flags) {
    bb_note_t* note = &core->chart.notes[idx];
    int32_t points = 5;

    switch (j) {
        case BB_JUDGE_PERFECT: points = 100; core->stats.perfect++; break;
        case BB_JUDGE_GREAT:   points = 50;  core->stats.great++;   break;
        case BB_JUDGE_NICE:    points = 10;  core->stats.nice++;    break;
        default:               points = 5;   core->stats.meh++;     break;
    }

    core->stats.hit++;
    core->stats.registered++;
    core->stats.combo++;
    if (core->stats.combo > core->stats.max_combo) {
        core->stats.max_combo = core->stats.combo;
    }
    core->stats.score += (points * core->score_mult_x10) / 10; /* GameManager.calculate_modified_score */

    /* Lives.regen_health: PERFECT only, never for ignore_animation hits */
    if (j == BB_JUDGE_PERFECT && !ignore_anim && core->stats.health < core->stats.max_health) {
        core->stats.health++;
    }

    core->stats.last_judgment = j;
    core->stats.last_hit_delta_us = signed_delta_us;
    core->stats.last_judgment_time_us = core->song_cur_time_us;
    core->stats.last_input_type = note->input_type;

    if (ignore_anim) {
        evt_flags |= BB_EVTF_IGNORE_ANIM;
    }
    bb_push_event(core, BB_EVT_HIT, idx, j, evt_flags, note->input_type);
}

/* NoteDetector.miss_note (LATE) + Lives.note_miss + game.gd registration. */
static void bb_late_miss(bb_rhythm_core_t* core, uint32_t idx, int64_t penalty_time_us) {
    bb_note_t* note = &core->chart.notes[idx];

    core->stats.last_judgment = BB_JUDGE_PASSED;
    core->stats.last_hit_delta_us = note->hit_delta_us;
    core->stats.last_judgment_time_us = core->song_cur_time_us;
    core->stats.last_input_type = note->input_type;

    if (core->miss_cooldown_active && penalty_time_us < core->miss_cooldown_until_us) {
        return; /* MissForgiveness: note disabled, no registration, no damage */
    }

    core->stats.missed++;
    core->stats.registered++;
    core->stats.combo = 0;
    core->miss_cooldown_active = true;
    core->miss_cooldown_until_us = penalty_time_us + BB_MISS_COOLDOWN_US;
    bb_push_event(core, BB_EVT_MISS, idx, BB_JUDGE_PASSED, 0, note->input_type);

    if (core->no_fail) {
        return;
    }
    if (core->hardcore) {
        core->stats.health = 0;
        core->state = BB_STATE_FAILED;
        bb_push_event(core, BB_EVT_FAILED, idx, BB_JUDGE_PASSED, 0, note->input_type);
        return;
    }
    /* Lives.gd: health -= damage; fail when health < 0 */
    core->stats.health -= (core->damage > 0 ? core->damage : 1);
    if (core->stats.health < 0) {
        core->stats.health = 0;
        core->state = BB_STATE_FAILED;
        bb_push_event(core, BB_EVT_FAILED, idx, BB_JUDGE_PASSED, 0, note->input_type);
    }
}

/* Finish a held note: graded by distance to the tail, always a registered hit. */
static void bb_finish_hold(bb_rhythm_core_t* core, uint32_t idx, int64_t abs_dist_us, int64_t signed_delta_us) {
    bb_note_t* note = &core->chart.notes[idx];
    bb_judgment_t j = bb_grade_distance(abs_dist_us);
    note->active = 0;
    note->holding = 0;
    note->result = j;
    core->held_idx[note->input_type] = BB_NO_HOLD;
    bb_register_hit(core, idx, j, signed_delta_us, true, BB_EVTF_HOLD_END);
    bb_advance_first_active_note(core);
}

static void bb_start_hold(bb_rhythm_core_t* core, uint32_t idx, bb_judgment_t j, int64_t signed_delta_us) {
    bb_note_t* note = &core->chart.notes[idx];
    note->holding = 1;
    note->hit_delta_us = signed_delta_us;
    core->held_idx[note->input_type] = idx;
    bb_register_hit(core, idx, j, signed_delta_us, false, BB_EVTF_HOLD_START);
}

void bb_core_init(bb_rhythm_core_t* core, const bb_chart_t* chart) {
    if (!core) {
        return;
    }
    memset(core, 0, sizeof(*core));
    core->score_mult_x10 = 10;
    core->damage = 1;
    core->extra_health = 0;
    if (chart) {
        core->chart = *chart;
    }
    bb_core_restart(core);
}

void bb_core_restart(bb_rhythm_core_t* core) {
    if (!core) {
        return;
    }
    bool keep_no_fail = core->no_fail;
    bool keep_hardcore = core->hardcore;
    bool keep_autoplay = core->autoplay;
    int64_t keep_calib = core->calibration_offset_us;

    memset(&core->stats, 0, sizeof(core->stats));
    core->stats.max_health = 4 + (core->extra_health > 0 ? core->extra_health : 0);
    core->stats.health = core->stats.max_health;
    core->stats.grade = BB_GRADE_NONE;

    for (uint32_t i = 0; i < core->chart.note_count; i++) {
        core->chart.notes[i].active = 1;
        core->chart.notes[i].holding = 0;
        core->chart.notes[i].result = BB_JUDGE_NONE;
        core->chart.notes[i].hit_delta_us = 0;
    }

    core->no_fail = keep_no_fail;
    core->hardcore = keep_hardcore;
    core->autoplay = keep_autoplay;
    core->calibration_offset_us = keep_calib;

    core->state = BB_STATE_IDLE;
    core->clock_started = false;
    core->song_pos_us = 0;
    core->song_cur_time_us = core->calibration_offset_us - core->chart.song_offset_us;
    core->prev_cur_time_us = core->song_cur_time_us;
    core->paused_song_pos_us = 0;
    core->current_beat = -1;
    core->current_bpm_milli = (core->chart.initial_bpm_milli > 0) ? core->chart.initial_bpm_milli : 126000;

    core->first_active_note_idx = 0;
    core->next_shutter_idx = 0;
    core->shutter_fired_count = 0;
    core->last_shutter_Zero_us = -10000000LL;
    core->shutter_active = false;

    core->current_loop_idx = 0;
    core->loop_switch_count = 0;

    core->miss_cooldown_active = false;
    core->miss_cooldown_until_us = 0;

    for (int i = 0; i < BB_INPUT_COUNT; i++) {
        core->held_idx[i] = BB_NO_HOLD;
    }
    core->hold_score_rem_us = 0;
    core->evt_head = 0;
    core->evt_count = 0;
}

void bb_core_start(bb_rhythm_core_t* core, int64_t initial_audio_pos_us) {
    if (!core) {
        return;
    }
    core->state = BB_STATE_PLAYING;
    core->clock_started = false;
    bb_update_clock(core, initial_audio_pos_us);
    core->prev_cur_time_us = core->song_cur_time_us;
}

void bb_core_pause(bb_rhythm_core_t* core) {
    if (!core || core->state != BB_STATE_PLAYING) {
        return;
    }
    core->paused_song_pos_us = core->song_pos_us;
    core->state = BB_STATE_PAUSED;
}

void bb_core_resume(bb_rhythm_core_t* core) {
    if (!core || core->state != BB_STATE_PAUSED) {
        return;
    }
    core->state = BB_STATE_PLAYING;
    core->prev_cur_time_us = core->song_cur_time_us; /* no hold score for the paused interval */
}

static void bb_process_holds(bb_rhythm_core_t* core) {
    int64_t held_us = 0;
    for (int lane = 0; lane < BB_INPUT_COUNT; lane++) {
        uint32_t idx = core->held_idx[lane];
        if (idx == BB_NO_HOLD) {
            continue;
        }
        bb_note_t* n = &core->chart.notes[idx];
        int64_t from = core->prev_cur_time_us;
        int64_t to = core->song_cur_time_us;
        if (from < n->timestamp_us) from = n->timestamp_us;
        if (to > n->hold_end_us) to = n->hold_end_us;
        if (to > from) {
            held_us += to - from;
        }
        /* HoldNote._process: song_position_offset > hold_end -> automatic PERFECT-distance completion */
        if (core->song_cur_time_us > n->hold_end_us) {
            bb_finish_hold(core, idx, 0, core->song_cur_time_us - n->hold_end_us);
        }
    }
    if (held_us > 0) {
        core->hold_score_rem_us += held_us * BB_HOLD_POINTS_PER_SEC;
        int64_t pts = core->hold_score_rem_us / 1000000LL;
        core->hold_score_rem_us -= pts * 1000000LL;
        core->stats.score += (int32_t)pts;
    }
}

void bb_core_step(bb_rhythm_core_t* core, int64_t raw_audio_pos_us) {
    if (!core || core->state != BB_STATE_PLAYING) {
        return;
    }

    bb_update_clock(core, raw_audio_pos_us);

    /* 1. Shutter events */
    while (core->next_shutter_idx < core->chart.shutter_count &&
           core->song_cur_time_us >= core->chart.shutter_times_us[core->next_shutter_idx]) {
        core->last_shutter_Zero_us = core->chart.shutter_times_us[core->next_shutter_idx];
        bb_push_event(core, BB_EVT_SHUTTER, core->next_shutter_idx, BB_JUDGE_NONE, 0, 0);
        core->shutter_fired_count++;
        core->next_shutter_idx++;
    }
    core->shutter_active = (core->shutter_fired_count > 0) &&
                           (core->song_cur_time_us >= core->last_shutter_Zero_us) &&
                           ((core->song_cur_time_us - core->last_shutter_Zero_us) <= BB_SHUTTER_DURATION_US);

    /* 2. Animation loop sections */
    while (core->current_loop_idx + 1 < core->chart.loop_count &&
           core->song_cur_time_us >= core->chart.loops[core->current_loop_idx + 1].timestamp_us) {
        core->current_loop_idx++;
        core->loop_switch_count++;
        bb_push_event(core, BB_EVT_LOOP, core->current_loop_idx, BB_JUDGE_NONE, 0, 0);
    }

    /* 3. Holds in progress (score accrual, tail completion) */
    bb_process_holds(core);

    /* 4. Autoplay / AUTO notes and late (+200 ms) misses, in timestamp order */
    bb_advance_first_active_note(core);
    for (uint32_t i = core->first_active_note_idx;
         i < core->chart.note_count && core->state == BB_STATE_PLAYING; i++) {
        bb_note_t* note = &core->chart.notes[i];
        if (!note->active || note->holding) {
            continue;
        }

        if ((core->autoplay || note->modifier == BB_MOD_AUTO) &&
            core->song_cur_time_us >= note->timestamp_us) {
            if (note->modifier == BB_MOD_HOLD && note->hold_end_us > note->timestamp_us) {
                bb_start_hold(core, i, BB_JUDGE_PERFECT, 0);
            } else {
                note->active = 0;
                note->result = BB_JUDGE_PERFECT;
                note->hit_delta_us = 0;
                bb_register_hit(core, i, BB_JUDGE_PERFECT, 0, note->modifier == BB_MOD_GHOST, 0);
            }
            continue;
        }

        if (core->song_cur_time_us > (note->timestamp_us + BB_NOTE_PASS_WINDOW_US)) {
            note->active = 0;
            note->result = BB_JUDGE_PASSED;
            note->hit_delta_us = core->song_cur_time_us - note->timestamp_us;
            core->stats.passed_notes++;
            bb_late_miss(core, i, note->timestamp_us + BB_NOTE_PASS_WINDOW_US);
            continue;
        }

        break; /* notes are sorted: first not-yet-expired, not-yet-due note ends the scan */
    }
    bb_advance_first_active_note(core);

    /* 5. Stage completion: outro starts at last note + 0.3 s (game_outro_video.gd) */
    if (core->state == BB_STATE_PLAYING &&
        core->chart.note_count > 0 &&
        core->first_active_note_idx >= core->chart.note_count) {
        int64_t end_us = core->chart.notes[core->chart.note_count - 1].timestamp_us;
        for (uint32_t i = 0; i < core->chart.note_count; i++) {
            if (core->chart.notes[i].modifier == BB_MOD_HOLD && core->chart.notes[i].hold_end_us > end_us) {
                end_us = core->chart.notes[i].hold_end_us;
            }
        }
        if (core->song_cur_time_us >= end_us + BB_STAGE_CLEAR_HOLD_US) {
            core->stats.grade = bb_core_calculate_grade(&core->stats);
            core->state = BB_STATE_COMPLETED;
            bb_push_event(core, BB_EVT_COMPLETED, 0, BB_JUDGE_NONE, 0, 0);
        }
    }
    if (core->state == BB_STATE_FAILED) {
        core->stats.grade = bb_core_calculate_grade(&core->stats);
    }
}

bb_judgment_t bb_core_press(bb_rhythm_core_t* core, uint8_t input_type, int64_t raw_audio_pos_us) {
    if (!core || core->state != BB_STATE_PLAYING || core->autoplay || input_type >= BB_INPUT_COUNT) {
        return BB_JUDGE_NONE;
    }

    /* Bring the timeline to the exact press instant first */
    bb_core_step(core, raw_audio_pos_us);
    if (core->state != BB_STATE_PLAYING) {
        return BB_JUDGE_NONE;
    }

    /* NoteDetector._process: nothing happens once no notes remain active */
    bb_advance_first_active_note(core);
    if (core->first_active_note_idx >= core->chart.note_count) {
        return BB_JUDGE_NONE;
    }

    /* GameManager.get_next_active_note(input_type): first active, not-held note in this lane */
    uint32_t found = BB_NO_HOLD;
    for (uint32_t i = core->first_active_note_idx; i < core->chart.note_count; i++) {
        const bb_note_t* n = &core->chart.notes[i];
        if (n->active && !n->holding && n->modifier != BB_MOD_AUTO && n->input_type == input_type) {
            found = i;
            break;
        }
    }

    int64_t signed_delta_us = 0;
    if (found != BB_NO_HOLD) {
        signed_delta_us = core->song_cur_time_us - core->chart.notes[found].timestamp_us;
    }

    if (found == BB_NO_HOLD || bb_abs64(signed_delta_us) > BB_MAX_HIT_DISTANCE_US) {
        /* note_absent: -20 score, combo reset, registered MISS, no health damage, note stays */
        core->stats.score -= BB_ABSENT_PENALTY;
        core->stats.combo = 0;
        core->stats.absent++;
        core->stats.registered++;
        core->stats.last_judgment = BB_JUDGE_ABSENT;
        core->stats.last_hit_delta_us = signed_delta_us;
        core->stats.last_judgment_time_us = core->song_cur_time_us;
        core->stats.last_input_type = input_type;
        bb_push_event(core, BB_EVT_ABSENT, found, BB_JUDGE_ABSENT, 0, input_type);
        if (core->hardcore) { /* game.gd: Hardcore Lube also fails on note_absent */
            core->stats.health = 0;
            core->state = BB_STATE_FAILED;
            bb_push_event(core, BB_EVT_FAILED, 0, BB_JUDGE_ABSENT, 0, input_type);
        }
        return BB_JUDGE_ABSENT;
    }

    bb_note_t* note = &core->chart.notes[found];
    bb_judgment_t j = bb_grade_distance(bb_abs64(signed_delta_us));

    if (note->modifier == BB_MOD_HOLD) {
        bb_start_hold(core, found, j, signed_delta_us);
        return j;
    }

    note->active = 0;
    note->result = j;
    note->hit_delta_us = signed_delta_us;
    bb_register_hit(core, found, j, signed_delta_us, note->modifier == BB_MOD_GHOST, 0);
    bb_advance_first_active_note(core);
    return j;
}

bb_judgment_t bb_core_release(bb_rhythm_core_t* core, uint8_t input_type, int64_t raw_audio_pos_us) {
    if (!core || core->state != BB_STATE_PLAYING || core->autoplay || input_type >= BB_INPUT_COUNT) {
        return BB_JUDGE_NONE;
    }
    bb_core_step(core, raw_audio_pos_us);
    if (core->state != BB_STATE_PLAYING) {
        return BB_JUDGE_NONE;
    }
    uint32_t idx = core->held_idx[input_type];
    if (idx == BB_NO_HOLD) {
        return BB_JUDGE_NONE;
    }
    int64_t signed_delta_us = core->song_cur_time_us - core->chart.notes[idx].hold_end_us;
    bb_finish_hold(core, idx, bb_abs64(signed_delta_us), signed_delta_us);
    return core->chart.notes[idx].result;
}

int64_t bb_core_time_til_next_note_us(const bb_rhythm_core_t* core, uint32_t note_idx) {
    if (!core || note_idx + 1 >= core->chart.note_count) {
        return 0;
    }
    int64_t cur = core->chart.notes[note_idx].timestamp_us;
    for (uint32_t i = note_idx + 1; i < core->chart.note_count; i++) {
        const bb_note_t* n = &core->chart.notes[i];
        if (n->modifier == BB_MOD_GHOST) {
            continue;
        }
        if (bb_abs64(n->timestamp_us - cur) < 10) { /* is_equal_approx at microsecond scale */
            continue;
        }
        return n->timestamp_us - cur;
    }
    return 0;
}

float bb_core_get_accuracy_percent(const bb_stats_t* stats) {
    if (!stats || stats->registered == 0) {
        return 0.0f;
    }
    return ((float)stats->hit / (float)stats->registered) * 100.0f;
}

bb_grade_t bb_core_calculate_grade(const bb_stats_t* stats) {
    /* ScoreScreen.calculate_grade */
    if (!stats || stats->registered == 0) {
        return BB_GRADE_NONE;
    }
    /* Integer comparisons: accuracy < p/10 <=> hit*10 < registered*p */
    uint64_t h10 = (uint64_t)stats->hit * 10ULL;
    uint64_t r = (uint64_t)stats->registered;
    if (h10 < r * 6ULL) return BB_GRADE_F;
    if (h10 < r * 7ULL) return BB_GRADE_D;
    if (h10 < r * 8ULL) return BB_GRADE_C;
    if (h10 < r * 9ULL) return BB_GRADE_B;
    if (stats->hit < stats->registered) return BB_GRADE_A;
    if (stats->meh == 0 && stats->nice == 0) return BB_GRADE_SS;
    return BB_GRADE_S;
}

const char* bb_judgment_name(bb_judgment_t j) {
    switch (j) {
        case BB_JUDGE_NONE:    return "READY";
        case BB_JUDGE_MEH:     return "MEH";
        case BB_JUDGE_NICE:    return "NICE";
        case BB_JUDGE_GREAT:   return "GREAT";
        case BB_JUDGE_PERFECT: return "PERFECT";
        case BB_JUDGE_ABSENT:  return "MISS (NO NOTE)";
        case BB_JUDGE_PASSED:  return "MISS (LATE)";
        default:               return "UNKNOWN";
    }
}

const char* bb_grade_name(bb_grade_t g) {
    switch (g) {
        case BB_GRADE_SS: return "SS";
        case BB_GRADE_S:  return "S";
        case BB_GRADE_A:  return "A";
        case BB_GRADE_B:  return "B";
        case BB_GRADE_C:  return "C";
        case BB_GRADE_D:  return "D";
        case BB_GRADE_F:  return "F";
        default:          return "-";
    }
}

const char* bb_state_name(bb_game_state_t s) {
    switch (s) {
        case BB_STATE_IDLE:      return "IDLE";
        case BB_STATE_PLAYING:   return "PLAYING";
        case BB_STATE_PAUSED:    return "PAUSED";
        case BB_STATE_FAILED:    return "FAILED";
        case BB_STATE_COMPLETED: return "STAGE CLEAR";
        default:                 return "UNKNOWN";
    }
}
