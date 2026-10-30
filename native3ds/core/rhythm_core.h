/**
 * @file rhythm_core.h
 * @brief Platform-neutral Beat Banger rhythm & judgment engine (pure C99, no <3ds.h>).
 *
 * Rules are taken from the CURRENT game source (C:\Beat Banger, Godot 4.7):
 *   - prefabs/NoteDetector.gd: per-lane note lookup, hit window +-150 ms,
 *       PERFECT < 0.4*150 = 60 ms, GREAT < 75 ms, NICE < 112.5 ms, else MEH;
 *       press with no note of that lane inside the window = "absent"
 *       (-20 score, combo reset, counted as a registered MISS, NO health loss,
 *       note is NOT consumed); late = song time > timestamp + 200 ms;
 *       MissForgiveness 100 ms (forgiven misses are not registered/damaging).
 *   - prefabs/HoldNote.gd: pressing starts the hold (a registered hit graded by the
 *       start distance), +100 pts/s while held, release (or reaching the tail) is a
 *       second registered hit graded by distance to hold_end_timestamp (never a miss).
 *   - prefabs/GhostNote.gd: ordinary hittable note with ignore_animation (no character
 *       animation, no health regeneration).
 *   - prefabs/Lives.gd: 4 health, miss -1, fail when health < 0, PERFECT hit +1
 *       (not for ignore_animation hits).
 *   - scenes/ScoreScreen.gd: grade from hit/registered (F<.6 D<.7 C<.8 B<.9 A<1,
 *       100% = S, SS if no Meh/Nice).
 *   - scenes/game_outro_video.gd: outro starts at last note + 0.3 s.
 *
 * Single gameplay timeline: chart_time = audio_pos + calibration - song_offset.
 * (util.gd get_game_audio_stream_pos adds calibration_offset; game_music.gd subtracts
 * song_offset.)  note_offset is stored for reference but, as in the game, not applied.
 */
#ifndef BB_RHYTHM_CORE_H
#define BB_RHYTHM_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Timing window constants in microseconds */
#define BB_MAX_HIT_DISTANCE_US    150000LL  /* 150.0 ms */
#define BB_PERFECT_WINDOW_US       60000LL  /*  60.0 ms (40% of 150 ms) */
#define BB_GREAT_WINDOW_US         75000LL  /*  75.0 ms (50% of 150 ms) */
#define BB_NICE_WINDOW_US         112500LL  /* 112.5 ms (75% of 150 ms) */
#define BB_MEH_WINDOW_US          150000LL  /* 150.0 ms (100% of 150 ms) */
#define BB_NOTE_PASS_WINDOW_US    200000LL  /* 200.0 ms past timestamp */
#define BB_STAGE_CLEAR_HOLD_US    300000LL  /* 300.0 ms after last note (game_outro_video.gd) */
#define BB_HOLD_POINTS_PER_SEC       100LL  /* HoldNote.gd: ceil(delta*100) per held frame */
#define BB_ABSENT_PENALTY             20    /* score_display.gd note_absent */
#define BB_MISS_COOLDOWN_US       100000LL  /* 100.0 ms MissTimer cooldown */
#define BB_SHUTTER_DURATION_US    350000LL  /* 350.0 ms visual shutter flash */

/* Capacity bounds for static embedded allocation on 3DS and host */
#define BB_MAX_NOTES              1024   /* SD-card mods: Holorutting "Extra Hard" has 529 */
#define BB_MAX_SHUTTERS             64
#define BB_MAX_LOOPS               256   /* SD-card mods: Cathy Does It All! Extra Spicy has 131 loop keyframes */
#define BB_MAX_BPM_CHANGES          16
#define BB_NO_HOLD          0xFFFFFFFFu

/* Input lane types (matches Enums.ACTION_0 .. ACTION_3) */
typedef enum {
    BB_INPUT_A = 0, /* Lane 0: A button */
    BB_INPUT_B = 1, /* Lane 1: B button */
    BB_INPUT_X = 2, /* Lane 2: X button (Normal/Expert) */
    BB_INPUT_Y = 3, /* Lane 3: Y button (Expert) */
    BB_INPUT_COUNT = 4
} bb_input_type_t;

/* Note modifier types (matches Enums.MODIFIER) */
typedef enum {
    BB_MOD_NORMAL = 0,
    BB_MOD_AUTO   = 1,
    BB_MOD_GHOST  = 2,  /* hittable, ignore_animation */
    BB_MOD_HOLD   = 3   /* hold_end_timestamp required */
} bb_note_modifier_t;

/* Judgment accuracy result (matches Enums.ACCURACY + miss reasons) */
typedef enum {
    BB_JUDGE_NONE        = 0,
    BB_JUDGE_MEH         = 1, /* +5 pts */
    BB_JUDGE_NICE        = 2, /* +10 pts */
    BB_JUDGE_GREAT       = 3, /* +50 pts */
    BB_JUDGE_PERFECT     = 4, /* +100 pts, +1 health */
    BB_JUDGE_ABSENT      = 5, /* Press with no note of that lane within 150 ms (-20, no health loss) */
    BB_JUDGE_PASSED      = 7  /* Note passed +200 ms without being hit */
} bb_judgment_t;

/* Final letter grades (matches Enums.GRADE) */
typedef enum {
    BB_GRADE_NONE = 0,
    BB_GRADE_F    = 1,
    BB_GRADE_D    = 2,
    BB_GRADE_C    = 3,
    BB_GRADE_B    = 4,
    BB_GRADE_A    = 5,
    BB_GRADE_S    = 6,
    BB_GRADE_SS   = 7
} bb_grade_t;

/* Gameplay state machine */
typedef enum {
    BB_STATE_IDLE      = 0,
    BB_STATE_PLAYING   = 1,
    BB_STATE_PAUSED    = 2,
    BB_STATE_FAILED    = 3,
    BB_STATE_COMPLETED = 4
} bb_game_state_t;

/* Individual note definition & runtime status */
typedef struct {
    int64_t        timestamp_us;   /* Chart timestamp in microseconds (relative to beat 0) */
    uint8_t        input_type;     /* bb_input_type_t (0..3) */
    uint8_t        modifier;       /* bb_note_modifier_t (0..3) */
    uint8_t        trigger_voice;  /* 1 if voice sample triggers on hit */
    uint8_t        active;         /* 1 while note can still be hit/passed, 0 once resolved */
    uint8_t        holding;        /* 1 while a hold note is being held */
    int64_t        hold_end_us;    /* Hold tail timestamp (BB_MOD_HOLD only) */
    bb_judgment_t  result;         /* Final judgment assigned to this note */
    int64_t        hit_delta_us;   /* Signed timing error (song_cur_time_us - timestamp_us) */
} bb_note_t;

/* BPM modifier change entry */
typedef struct {
    int64_t  timestamp_us; /* Time in microseconds when BPM takes effect */
    uint32_t bpm_milli;    /* BPM * 1000 (e.g. 126000 for 126.0 BPM) */
} bb_bpm_change_t;

/* Animation loop section switch entry */
typedef struct {
    int64_t  timestamp_us; /* Time in microseconds when loop section starts */
    char     name[32];     /* Loop section identifier (e.g. "cathy_e1_headlick") */
} bb_loop_section_t;

/* Portable chart container */
typedef struct {
    char              stage_id[32];     /* e.g. "cathy_e1" */
    char              stage_title[48];  /* e.g. "Virgin Newbie" */
    char              difficulty[24];   /* e.g. "Virgin", "Normal", "Expert" */
    uint8_t           rating;           /* Difficulty index (0, 1, 2) */
    uint32_t          initial_bpm_milli;/* Initial BPM * 1000 (e.g. 126000) */
    int64_t           song_offset_us;   /* settings.cfg song_offset in microseconds */
    int64_t           note_offset_us;   /* settings.cfg note_offset in microseconds */

    uint32_t          note_count;
    bb_note_t         notes[BB_MAX_NOTES];

    uint32_t          shutter_count;
    int64_t           shutter_times_us[BB_MAX_SHUTTERS];

    uint32_t          loop_count;
    bb_loop_section_t loops[BB_MAX_LOOPS];

    uint32_t          bpm_change_count;
    bb_bpm_change_t   bpm_changes[BB_MAX_BPM_CHANGES];
} bb_chart_t;

/* Cumulative gameplay statistics */
typedef struct {
    uint32_t perfect;
    uint32_t great;
    uint32_t nice;
    uint32_t meh;
    uint32_t hit;           /* Registered non-miss entries (hold start and end count separately) */
    uint32_t missed;        /* Registered misses from late notes (forgiven misses excluded) */
    uint32_t absent;        /* Presses with no hittable note (registered misses) */
    uint32_t passed_notes;  /* Notes that expired past +200 ms (forgiven or not) */
    uint32_t registered;    /* Accuracy denominator (GameManager.registered_notes) */

    uint32_t combo;
    uint32_t max_combo;
    int32_t  score;
    int32_t  health;
    int32_t  max_health;

    bb_judgment_t last_judgment;
    int64_t       last_hit_delta_us; /* Signed error: negative = early, positive = late */
    int64_t       last_judgment_time_us;
    uint8_t       last_input_type;

    bb_grade_t    grade;
} bb_stats_t;

/* Gameplay events for presentation layers (animation, SFX); drained with bb_core_poll_event */
typedef enum {
    BB_EVT_NONE = 0,
    BB_EVT_HIT,        /* note_idx, judgment, flags (hold start/end, ignore_animation) */
    BB_EVT_MISS,       /* counted late miss (not forgiven) */
    BB_EVT_ABSENT,     /* press with nothing to hit */
    BB_EVT_SHUTTER,
    BB_EVT_LOOP,       /* animation loop section switch, note_idx = loop index */
    BB_EVT_FAILED,
    BB_EVT_COMPLETED
} bb_event_type_t;

#define BB_EVTF_IGNORE_ANIM 0x01
#define BB_EVTF_HOLD_START  0x02
#define BB_EVTF_HOLD_END    0x04

typedef struct {
    bb_event_type_t type;
    uint32_t        note_idx;
    bb_judgment_t   judgment;
    uint8_t         flags;
    uint8_t         input_type;
    int64_t         chart_time_us;
} bb_event_t;

#define BB_EVENT_QUEUE_CAP 64

/* Complete rhythm engine instance */
typedef struct {
    bb_chart_t      chart;
    bb_game_state_t state;
    bb_stats_t      stats;

    /* Options */
    bool            no_fail;
    bool            hardcore;
    bool            autoplay;
    int64_t         calibration_offset_us; /* User/platform latency calibration offset */

    /* Power-ups (class/powerups.gd). Set after bb_core_init, before bb_core_restart/start; kept across restarts.
     *   score_mult_x10: hit points = base * mult / 10 (truncated), e.g. 5 = 0.5x, 20 = 2x, 40 = 4x
     *   damage:         health lost per counted miss (Double Edged Sword = 2)
     *   extra_health:   added to the 4 base hearts (Performance Pills = 3) */
    int32_t         score_mult_x10;
    int32_t         damage;
    int32_t         extra_health;

    /* Monotonic clock state */
    bool            clock_started;
    int64_t         song_pos_us;           /* Monotonic audio position in us */
    int64_t         song_cur_time_us;      /* chart time = song_pos_us + calibration_offset_us - song_offset_us */
    int64_t         prev_cur_time_us;      /* chart time at previous step (hold scoring) */
    int64_t         paused_song_pos_us;
    int32_t         current_beat;
    uint32_t        current_bpm_milli;

    /* Active cursors */
    uint32_t        first_active_note_idx;
    uint32_t        next_shutter_idx;
    uint32_t        shutter_fired_count;
    int64_t         last_shutter_Zero_us;  /* song_cur_time_us when most recent shutter fired */
    bool            shutter_active;

    uint32_t        current_loop_idx;
    uint32_t        loop_switch_count;

    /* MissForgiveness equivalent, in chart time */
    bool            miss_cooldown_active;
    int64_t         miss_cooldown_until_us;

    /* Hold state: index of held note per lane (BB_NO_HOLD if none) */
    uint32_t        held_idx[BB_INPUT_COUNT];
    int64_t         hold_score_rem_us;

    /* Event FIFO (oldest dropped on overflow) */
    bb_event_t      events[BB_EVENT_QUEUE_CAP];
    uint32_t        evt_head;
    uint32_t        evt_count;
} bb_rhythm_core_t;

/* Lifecycle & configuration */
void bb_core_init(bb_rhythm_core_t* core, const bb_chart_t* chart);
void bb_core_start(bb_rhythm_core_t* core, int64_t initial_audio_pos_us);
void bb_core_pause(bb_rhythm_core_t* core);
void bb_core_resume(bb_rhythm_core_t* core);
void bb_core_restart(bb_rhythm_core_t* core);

/* Advance timeline to monotonic audio position (in microseconds) */
void bb_core_step(bb_rhythm_core_t* core, int64_t raw_audio_pos_us);

/* Process player button press at the given audio position (in microseconds).
 * Returns the hit grade, BB_JUDGE_ABSENT, or BB_JUDGE_NONE if input is ignored. */
bb_judgment_t bb_core_press(bb_rhythm_core_t* core, uint8_t input_type, int64_t raw_audio_pos_us);

/* Process button release; ends a held hold note in that lane (graded by distance to its tail). */
bb_judgment_t bb_core_release(bb_rhythm_core_t* core, uint8_t input_type, int64_t raw_audio_pos_us);

/* Pop the oldest pending gameplay event; returns false when empty. */
bool bb_core_poll_event(bb_rhythm_core_t* core, bb_event_t* out);

/* GameManager.get_time_til_next_note: distance to the next non-ghost note with a different
 * timestamp (0 if none). Drives the character frame tween (duration = value * 5/6). */
int64_t bb_core_time_til_next_note_us(const bb_rhythm_core_t* core, uint32_t note_idx);

/* Grade calculation and string helpers */
bb_grade_t  bb_core_calculate_grade(const bb_stats_t* stats);
float       bb_core_get_accuracy_percent(const bb_stats_t* stats);
const char* bb_judgment_name(bb_judgment_t j);
const char* bb_grade_name(bb_grade_t g);
const char* bb_state_name(bb_game_state_t s);

#ifdef __cplusplus
}
#endif

#endif /* BB_RHYTHM_CORE_H */
