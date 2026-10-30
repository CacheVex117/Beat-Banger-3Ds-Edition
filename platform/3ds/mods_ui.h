/**
 * @file mods_ui.h
 * @brief Main menu > Mods: the mod manager on the bottom screen (the top keeps the pulsing logo).
 *
 * Hub (Library | Modifiers, side by side on the checkerboard), Library (every SD-card level from MOD2DS, sdmc:/3ds/bb_mods/) and a
 * mod's own screen modelled on the original's mod level card (title + character, round portrait with the MOD ribbon, sawtooth band).
 * Presentation only: main.c owns the state, input, preview music and the difficulty / modifiers / power-up flow.
 */
#ifndef BB_MODS_UI_H
#define BB_MODS_UI_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

typedef struct {
    const char* title;
    const char* character;
    const char* pack;        /* act_name (the mod pack) */
    const char* thumb_path;  /* <root>/<id>/menu/thumb.t3x (round portrait; may be missing) */
    int         best_grade;  /* best over the three difficulties, 0 = never cleared */
    long        best_score;
    bool        has_cutscenes;
} bb_mod_entry_t;

/* Thumbnails: loaded when the manager opens, freed when it closes. */
void bb_mods_open(const bb_mod_entry_t* entries, int n);
void bb_mods_close(void);

/* Hub: zones BB_ZONE_LEVEL + 0 (Library), + 1 (Modifiers), BB_ZONE_BACK. mod_count = levels listed; used / cap = mod levels on
 * the card (including any past the limit) against the mod level limit, shown as a counter and a Good / Okay / Near Full / Full gauge. */
void bb_mods_hub_bottom(int cursor, int mod_count, int used, int cap);
/* Library list: zones BB_ZONE_LEVEL + i per row, BB_ZONE_BACK. The header shows used / cap in the gauge colour. */
void bb_mods_library_bottom(const bb_mod_entry_t* entries, int n, int cursor, int used, int cap);
/* A mod's screen: zones BB_ZONE_LEVEL (portrait = select difficulty), BB_ZONE_MODS (modifiers), BB_ZONE_BACK. */
void bb_mods_detail_bottom(const bb_mod_entry_t* e, int index, bool portrait_armed);

#endif
