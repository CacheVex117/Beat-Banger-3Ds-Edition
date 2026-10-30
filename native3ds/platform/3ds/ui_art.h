/**
 * @file ui_art.h
 * @brief Menu artwork converted from the original game (romfs:/ui, tools/build_ui_assets.py): logo, room
 *        backgrounds, circular stage portraits, shop item icons, grade stars and shop category icons.
 */
#ifndef BB_UI_ART_H
#define BB_UI_ART_H

#include <stdbool.h>
#include <3ds.h>
#include <citro2d.h>

typedef enum { BB_BG_SHOP = 0, BB_BG_EXTRAS } bb_bg_t;

bool bb_art_init(void);          /* loads what exists; every draw call is a no-op for a missing texture */
void bb_art_shutdown(void);

void bb_art_logo(float x, float y, float w, float z);
void bb_art_background(bb_bg_t which, u8 brightness, float z);                 /* full 400x240 */
/* The (src_x, 0, src_w, 240) slice of a room background drawn 1:1 at (0,0): the 320-wide bottom screen. */
void bb_art_background_crop(bb_bg_t which, u8 brightness, float src_x, float src_w, float z);
/* A piece of the 400x240 background drawn where it belongs (used to put a foreground edge in front of a character). */
/* A source rectangle of the background stretched onto a destination rectangle (the shop camera zoom). */
void bb_art_background_view(bb_bg_t which, u8 brightness, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, float z);
void bb_art_background_part(bb_bg_t which, u8 brightness, float sx, float sy, float sw, float sh, float z);
void bb_art_thumb(const char* stage_id, float cx, float cy, float d, u32 tint, float z);
void bb_art_star(bool outline, float cx, float cy, float size, u32 tint, float z);
void bb_art_category(int idx, float cx, float cy, float size, u32 tint, float z); /* 0 power-up, 1 photo, 2 music, 3 outfit */
void bb_art_shop_icon(int item, int category_idx, float cx, float cy, float size, u32 tint, float z);

/* Menu art of the presentation pass (tools/build_menu_art.py) */
void bb_art_mainmenu(float t_s, float x, float y, u8 brightness, float z);          /* animated main-menu loop (320x240), draws nothing if missing */
bool bb_art_mainmenu_ready(void);
/* 0 = zoe_fix_main_menu, 1 = the naked loop (easter egg). The swap (free + load, ~4 MiB) happens inside the next bb_art_mainmenu draw. */
void bb_art_mainmenu_variant(int variant);   /* 0 = the theme's loop (default or Halloween) */
/* Calendar.GAME_THEME reskin: logo + main-menu loop (call before bb_art_init too, or right after it). */
void bb_art_set_theme(bool halloween);
/* The Change Theme button's icon (main_menu.tscn ChangeTheme icon_texture). */
void bb_art_theme_icon(float cx, float cy, float size, u32 tint, float z);
int  bb_art_mainmenu_current(void);
/* Power-up selection art (icon_large): 0 no power-up, 1 pills, 2 condom, 3 double edged sword, 4 blindfold, 5 lube. */
void bb_art_powerup(int idx, float cx, float cy, float size, u32 tint, float z);
/* The window of the original 1920x1080 menu video that the 320x240 loop shows (x, y, width; height = width * 3/4). */
void bb_art_mainmenu_crop(float* x, float* y, float* w);
/* The character's icon for difficulty `diff` (0..2) of a stage (DifficultyNode IconTextureRect); false when the stage has none. */
bool bb_art_diff_icon(const char* stage_id, int diff, float cx, float cy, float size, u32 tint, float z);
/* Grade star rotated by `angle` radians about its centre (menu star bursts, results particles). */
void bb_art_star_rot(bool outline, float cx, float cy, float size, float angle, u32 tint, float z);
/* 0 music 1 gallery 2 theater 3 characters 4 scene viewer 5 collection 6 trophy 7 back button */
void bb_art_icon(int idx, float cx, float cy, float size, u32 tint, float z);
/* Achievement art by name ("combo_king", "chad", ...); falls back to the trophy. */
void bb_art_ach_icon(const char* name, float cx, float cy, float size, u32 tint, float z);
/* An achievement's own grid icon (bb_ach_icon_name), scaled by its icon_scale_multiplier (0.85 for Chad / Gigachad / the honks). */
void bb_art_ach(int ach_id, float cx, float cy, float size, u32 tint, float z);

/* The original settings dialog frame (speakers, cables); 301x228 drawn with its top-left at (x, y). */
void bb_art_settings_frame(float x, float y, u8 alpha, float z);

/* Halloween skins (theme.h): hw_game cells 0 drippy heart, 1 candy-corn outline, 2 candy-corn filled, 3 candy particle (64 px);
 * the game's spider web (a negative w mirrors it); the level-select decorations 0 spooky_tree_2, 1 spooky_tree_4, 2 simple_web_1,
 * 3 simple_web_2 at their on-screen size times `scale`. Positions are centres. */
void bb_art_hw_cell(int cell, float cx, float cy, float size, float angle, u32 tint, float z);
void bb_art_hw_web(float cx, float cy, float w, float h, u32 tint, float z);
void bb_art_hw_level_deco(int k, float cx, float cy, float scale, float angle, u32 tint, float z);

#endif
