/**
 * @file companion.h
 * @brief The bottom screen outside gameplay and the main menu: a quiet companion to the top-screen menus (level records, your
 *        lane controls, setting help, cutscene / game-over progress, button hints). Presentation only; main.c fills a bb_comp_t
 *        every frame. Gameplay keeps its own highway (game_view.c) and Jasmine's Shop its own UI (screens.c).
 *        Everything here is a 3DS adaptation (the original is single-screen); remove the call in main.c to get the black
 *        bottom screen back.
 */
#ifndef BB_COMPANION_H
#define BB_COMPANION_H

#include <stdbool.h>
#include <stdint.h>
#include <3ds.h>
#include <citro2d.h>

typedef enum {
    BB_COMP_BLANK = 0,   /* backdrop + hints only */
    BB_COMP_TITLE,
    BB_COMP_HELP,        /* settings: one line of help for the highlighted row */
} bb_comp_kind_t;

typedef struct {
    bb_comp_kind_t kind;
    const char*    key[4];      /* button hints: "A", "B", "L/R", "START" ... */
    const char*    label[4];
    int            nhints;

    const char* stage_id;       /* level / difficulty */
    const char* character;
    const char* title;
    bool        open;
    int         grade[3];       /* 0 unplayed, 1..7 F..SS */
    int32_t     score[3];
    int         combo[3];
    int         diff;
    float       speed;
    bool        no_hud, no_beat_bar, hands_free_ready;
    int         ctrl_mode;

    const char* help;           /* settings / generic one-liner */


} bb_comp_t;

void bb_comp_clear(bb_comp_t* c, bb_comp_kind_t kind);
void bb_comp_hint(bb_comp_t* c, const char* key, const char* label);
/* Call after C2D_SceneBegin(bottom). Entrance motion follows the top screen's bb_ui_enter clock (and Reduce Motion). */
void bb_comp_draw(const bb_comp_t* c);

#endif
