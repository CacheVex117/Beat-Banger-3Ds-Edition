/**
 * @file menu_ui.h
 * @brief Top-screen menus laid out like the original game (main menu, level select, difficulty select with
 *        modifiers, options tabs). Presentation only; the caller owns state and input (main.c).
 */
#ifndef BB_MENU_UI_H
#define BB_MENU_UI_H

#include <stdbool.h>
#include <stdint.h>
#include <3ds.h>
#include <citro2d.h>

typedef struct {
    const char* id;
    const char* character;
    const char* title;
    bool        open;
    int         diffs_done;   /* difficulties with a recorded result (the level's 0..3 stars) */
    int         best_grade;   /* best grade over the difficulties, 0 = unplayed, 1..7 = F..SS (shown as a badge on the portrait) */
} bb_ui_level_t;

typedef struct { int grade; int32_t score; int combo; } bb_ui_diff_stat_t;   /* grade 0 = unplayed, 1..7 = F..SS */

void bb_ui_main_menu(C2D_TextBuf tb, int cursor, int rank, int money, int exp);
/* Touch-zone ids registered by the bottom-screen menus (ui_motion.h bb_ui_zone_*). */
enum { BB_ZONE_BACK = 1, BB_ZONE_PREV, BB_ZONE_NEXT, BB_ZONE_MODS, BB_ZONE_HANDSFREE, BB_ZONE_THEME, BB_ZONE_LEVEL = 100, BB_ZONE_ACT = 200, BB_ZONE_PAUSE = 300, BB_ZONE_FAIL = 400 };

/* Level select on the BOTTOM screen (portraits, rank, stars, name; zones: BB_ZONE_LEVEL + i, prev/next, back). */
void bb_ui_level_select(C2D_TextBuf tb, const bb_ui_level_t* lv, int n, int cursor, const char* act_name, int act_idx,
                        int act_count, int exp, const bool owned_powerups[5]);
/* Live touch drag of the portrait row, in portrait steps (0 when not dragging). */
void bb_ui_level_drag(float steps);
/* TOP screen while choosing a level: checker + a framed shot from the level's pre-level cutscene (image index), dark when locked,
 * and the volume markers under it. */
void bb_ui_level_card(const char* stage_id, int image, bool open, int act_idx, int act_count);
/* Loading screen: BOTTOM = the same framed shot alone + spinner / LOADING; TOP = the pulsing main-menu logo on the checker. */
void bb_ui_loading_bottom(const char* stage_id, int image);
void bb_ui_logo_top(void);
/* Main menu entries (original order): 0 Play, 1 Options, 2 Mods, 3 Shop, 4 Extras, 5 Achievements, 6 Quit. */
#define BB_MAIN_ITEMS 7
float bb_ui_main_item_y(int i);
/* The main menu's Change Theme button (bottom right of bb_ui_main_menu_bottom, zone BB_ZONE_THEME) lights when pressed. */
void bb_ui_theme_pressed(void);
/* The same top screen held still: Extras, Achievements and Options keep the main menu's pulsing logo with no transition. */
void bb_ui_logo_top_still(void);
/* Cutscene bottom screen: dimmed logo on the checker + Skip / Autoplay / Next (zones BB_ZONE_LEVEL + 0..2). */
void bb_ui_cutscene_bottom(bool autoplay);
void bb_ui_cutscene_tapped(int button);   /* 0 Skip, 1 Autoplay, 2 Next: the flash under that button */
void bb_ui_level_card_free(void);
/* Difficulty: TOP = checker + SELECT DIFFICULTY; BOTTOM = the cards (zones BB_ZONE_LEVEL + d, BB_ZONE_MODS, BB_ZONE_HANDSFREE, back). */
void bb_ui_difficulty_top(C2D_TextBuf tb);
void bb_ui_difficulty(C2D_TextBuf tb, const char* stage_id, const char* character, const char* title, const bb_ui_diff_stat_t st[3],
                      int cursor, float speed);
/* Modifiers: TOP = checker + MODIFIERS; BOTTOM = the rows (touch zones BB_ZONE_LEVEL + i, BB_ZONE_BACK) and the description. */
void bb_ui_modifiers_top(C2D_TextBuf tb);
/* legacy: -1 = not a Legacy level (3 rows), 0 / 1 = the LEGACY LAYOUT row, off / on (4 rows) */
void bb_ui_modifiers(C2D_TextBuf tb, int cursor, float speed, bool no_hud, bool no_beat_bar, int legacy);
/* Settings dialog (footage 2:42-2:50): category menu, then a page; slider[i] >= 0 shows a 0..10 slider row. */
void bb_ui_settings_menu(C2D_TextBuf tb, const char* const* names, int n, int cursor);
/* Draw the settings dialog for the bottom screen (from the pause menu): no backdrop, centred on 320 px, no footer; registers touch
 * zones BB_ZONE_LEVEL + i (entries / rows), BB_ZONE_ACT + i (slider bars) and BB_ZONE_BACK. */
void bb_ui_settings_on_bottom(bool on);
void bb_ui_settings_page(C2D_TextBuf tb, const char* title, const char* const* labels, const char* const* values, const int* slider, int n, int cursor);
/* Main menu, bottom screen: the original menu video full screen with the six entries over its left side (dim 0..255; show_items false =
 * the video only, behind the achievements drawer). Call after C2D_SceneBegin(bottom). */
void bb_ui_main_menu_bottom(int cursor, u8 dim_level, bool show_items);
/* Touch: the entry under (px, py) on the bottom screen, or -1. */
int  bb_ui_main_menu_hit(int px, int py, int cursor);
/* Touch on the original's BoobClick area: star burst at the touch, returns the tap count 1..5 (5 = the naked loop starts behind the
 * shutters), 0 if outside or already done. bb_ui_egg_reset: back to the default loop (a new main-menu scene in the original). */
int  bb_ui_egg_tap(int px, int py);
void bb_ui_egg_reset(void);
/* The main-menu stage backdrop (character loop, logo, dim menu words); dim 0..255. Used behind dialogs and panels. */
void bb_ui_main_backdrop(C2D_TextBuf tb, u8 dim_level);
/* Soft screen-edge vignette drawn over every top-screen menu (just under the fade): gives the menus one lit-from-the-centre look. */
void bb_ui_vignette(void);
/* Settings > Credits: the original's credits list in the settings frame; dir = -1 / 0 / +1 scrolls (it also rolls by itself). */
void bb_ui_credits(C2D_TextBuf tb, int dir);
/* Settings > Gameplay > Calibration Menu (calibrator.tscn): four beat nodes pulsing on the calibrator jam (100 BPM), current offset in ms. */
void bb_ui_calibration(C2D_TextBuf tb, int ms, int beat, float beat_s);

/* Shared colours: the character name tint of the level select, and the grade badge colours (0 unplayed, 1..7 = F..SS). */
void        bb_ui_character_rgb(const char* character, u8* r, u8* g, u8* b);
const char* bb_ui_grade_name(int grade);
void        bb_ui_grade_rgb(int grade, u8 out[3]);

#endif
