/**
 * @file flow_ui.h
 * @brief Play-flow overlays: the results screen (bottom screen, the original ScoreScreen, with the reward banners; the top dims), pause menu,
 *        resume count-in and game-over text (top). Presentation only; main.c owns state and input.
 */
#ifndef BB_FLOW_UI_H
#define BB_FLOW_UI_H

#include <stdbool.h>
#include <stdint.h>
#include <3ds.h>
#include <citro2d.h>

#define BB_RESULTS_ANIM_MS 4000   /* ScoreScreen show_stars: the buttons appear (and A continues) after this long */

typedef struct {
    char     stage[48];           /* level title */
    char     sub[48];             /* "Cathy - Virgin" */
    char     grade[4];            /* "S", "SS", "A", ... */
    int      grade_n;             /* 1..7 (F..SS) stars */
    int32_t  score, pb;
    float    accuracy;            /* 0..100 */
    uint32_t perfect, great, nice, meh, miss, max_combo;
    uint32_t hit;                 /* "Notes Hit" (registered hits, hold starts and ends counted separately like the original) */
    bool     new_best;
    bool     rewards;             /* payout / EXP were granted (not for autoplay, test runs, no-fail) */
    int32_t  money, exp_gain, exp_total;
    int      rank_before, rank_after;
    char     toast[4][44];        /* achievements ("Taste Of Victory") and unlocks, cycled at the end */
    uint8_t  toast_kind[4];       /* 0 achievement, 1 unlock */
    int      toast_n;
} bb_results_t;

void bb_results_begin(void);                                       /* a results screen starts (resets the sound schedule) */
void bb_results_update(const bb_results_t* r, int64_t elapsed_ms); /* call every frame: fires the timed sounds */
/* Bottom screen (call after C2D_SceneBegin(bottom), over the frozen highway): focus 0 = Continue, 1 = Retry. */
void bb_results_draw(const bb_results_t* r, int64_t elapsed_ms, int focus);
/* Touch on the bottom screen: 0 Continue, 1 Retry, -1 nothing (buttons exist once the sequence is done). */
int  bb_results_hit(int px, int py, int64_t elapsed_ms);
/* Top screen: dims the frozen stage scene in step with the results (payout / trophy banners are on the bottom, bb_results_draw). */
void bb_results_draw_top(const bb_results_t* r, int64_t elapsed_ms);

/* Pause (PauseMenu.tscn): TOP = dim + the waving "Paused" over the frozen stage; BOTTOM = the black sawtooth panel with the level line and
 * Resume / Restart / Settings / Level Select / Exit To Menu (touch zones BB_ZONE_PAUSE + i). t_ms = time since the pause opened. */
#define BB_PAUSE_ITEMS 5
void bb_flow_pause_top(int64_t t_ms, float alpha);
void bb_flow_pause_bottom(int cursor, const char* level_line, int64_t t_ms, float alpha);
/* Resume countdown (PauseMenu "countdown", 3 s, top screen) and the pause menu's fade-out during it. */
void bb_flow_countin(float t_s);
float bb_flow_countin_menu_alpha(float t_s);
/* Level failed (GameOver.tscn): t_ms = time since the fail (the screen starts at once). The text appears small on the BOTTOM screen,
 * rises onto the TOP screen (wiped to black) and settles there; the bottom fades to the game-over art with the caption and Retry / Give Up
 * (zones BB_ZONE_FAIL + 0 / 1, focus 0 = Retry). */
void bb_flow_fail_top(int64_t t_ms, uint32_t caption);
void bb_flow_fail_bottom(int64_t t_ms, int focus, uint32_t caption);

#endif
