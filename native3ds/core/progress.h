/**
 * @file progress.h
 * @brief Economy and meta-progression rules of the original game (pure C99): money, EXP, ranks, shop purchases,
 *        power-ups, collectables and achievements. Values come from the reference source:
 *   class/player.gd (ranks), class/save.gd (payouts/EXP), class/powerups.gd, class/items.gd, class/ach.gd,
 *   scenes/achievement_check.gd, resources/shop_items/(*.tres).
 * All state lives in bb_save_t.player (save_data.h); nothing here does I/O.
 */
#ifndef BB_PROGRESS_H
#define BB_PROGRESS_H

#include <stdint.h>
#include <stdbool.h>
#include "save_data.h"

/* ---- item enums (class/items.gd) ---- */
enum {
    BB_ITEM_NONE = 0,
    BB_ITEM_POWERUP_EXTRA_LIVES = 100,
    BB_ITEM_POWERUP_NO_FAIL = 101,
    BB_ITEM_POWERUP_DOUBLE_EDGED = 102,
    BB_ITEM_POWERUP_BLINDFOLD = 103,
    BB_ITEM_POWERUP_HARDCORE = 104,
    BB_ITEM_GALLERY_CATHY_PANTS = 301,
    BB_ITEM_GALLERY_ZOE_SERIOUSLY = 306,
    BB_ITEM_GALLERY_GALE_BREAST_EXPAND = 311,
    BB_ITEM_GALLERY_CLAIRE_PILEDRIVER = 316,
    BB_ITEM_GALLERY_FRENEMIES = 320,       /* achievement gallery: In The Closet on Expert */
    BB_ITEM_GALLERY_STUDIO_TOUR = 321,     /* achievement gallery: In The Dorms on Expert */
    BB_ITEM_COLLECT_DORM_KEY = 723,        /* collectable for clearing In The Dorms (507) */
    BB_ITEM_COLLECT_DAWNA_PHOTO = 721,     /* collectable: touching Dawna in Characters (lore_character.gd) */
    BB_ITEM_COLLECT_LICENSE = 709,         /* collectable: EXP > 500 (main_menu.gd) */
    BB_ITEM_EGG_DORM_KEY_READ = 901,       /* Items.EGGS: reading the Dorm Key in the Collection; After Class's level_prereq */
    BB_ITEM_GALLERY_JASMINE_TITFUCK = 399
};

#define BB_MONEY_CAP 99999
#define BB_EXP_CAP   9999

/* ---- ranks (class/player.gd) ---- */
#define BB_RANK_COUNT 8
const char* bb_rank_title(int rank);
int         bb_rank_required_exp(int rank);
int         bb_rank_from_exp(int exp);          /* current rank index */
int         bb_rank_next(int exp);              /* next rank index (== current at the top) */

/* ---- rewards ---- */
int32_t bb_exp_for_difficulty(int diff);        /* 50 / 100 / 150 */
int32_t bb_money_payout(int32_t score);         /* Save.score_payout: round(score * 0.1), never negative */
void    bb_give_money(bb_save_t* s, int32_t amount);
void    bb_give_exp(bb_save_t* s, int32_t amount);

/* ---- items / shop ---- */
typedef enum { BB_BUY_OK = 0, BB_BUY_OWNED, BB_BUY_RANK_LOCKED, BB_BUY_NO_MONEY } bb_buy_result_t;
bool            bb_item_owned(const bb_save_t* s, int item);
bb_buy_result_t bb_shop_buy(bb_save_t* s, int item, int32_t price, int32_t required_exp);
bool            bb_has_powerups(const bb_save_t* s);

/* ---- power-ups (class/powerups.gd) ---- */
typedef struct {
    int32_t score_mult_x10;   /* 10 = 1.0x */
    int32_t damage;
    int32_t extra_health;
    bool    no_fail;
    bool    hardcore;
    bool    disappearing;     /* The Blindfold */
} bb_powerup_effects_t;
bb_powerup_effects_t bb_powerup_effects(int item);
const char* bb_powerup_title(int item);

/* ---- collectables unlocked by beating levels (Items.unlock_collectable_by_level) ---- */
int bb_collectable_for_level(int level_enum);   /* 0 if none */

/* ---- achievements: the Achievements grid (scenes/Achievements/achievements.tscn, 5 columns) in its order; rewards from class/ach.gd,
 *      titles and descriptions from locale/po/en.po ---- */
#define BB_ACH_COUNT 17
typedef struct {
    uint16_t    id;
    const char* name;
    const char* desc;        /* plain text (BBCode stripped) */
    int32_t     reward;
    const char* hl;          /* the description's coloured [wave][color] span ("" if none), a substring of desc */
    uint32_t    hl_rgb;      /* its colour, 0xRRGGBB (orange FFA500, red, green, pink) */
    const char* icon;        /* bb_art_ach_icon cell name (romfs:/ui/ach_icons.txt) */
    uint8_t     icon_pct;    /* AchievementNode.icon_scale_multiplier x 100 (85 for Chad / Gigachad / the honks) */
    bool        evaluable;   /* the port can award it (false: Something Different needs mods, Eyes Up Here a shop stare timer) */
} bb_ach_def_t;
const bb_ach_def_t* bb_ach_defs(void);   /* BB_ACH_COUNT entries, grid order */
const bb_ach_def_t* bb_ach_find(int id); /* grid entries plus class/ach.gd's unused ones (no grid tile, never awarded) */
/* Icon cell name for an achievement ("trophy" if unknown). */
const char* bb_ach_icon_name(int id);
/* Achievements.check_unlocked_achievements: unlock the stat-tracked ones (Taste Of Victory: levels beaten >= 1; the three nose honks:
 * BB_STAT_NOSE_HONKS >= 20 / 50 / 100). Writes newly unlocked ids to new_ids (up to max); returns how many. */
int  bb_ach_check_stats(bb_save_t* s, int* new_ids, int max);
/* Progress toward an achievement for the info popup (AchievementNode.get_achievement_progress): value / max; 0/1 or 1/1 without a stat. */
void bb_ach_progress(const bb_save_t* s, int id, int32_t* value, int32_t* max);
bool bb_ach_unlocked(const bb_save_t* s, int id);
/* Unlock (reward = money, +10 EXP). Returns true if newly unlocked. */
bool bb_ach_unlock(bb_save_t* s, int id);

enum {
    BB_ACH_TASTEOFVICTORY = 1001, BB_ACH_COMBOKING = 1004,
    BB_ACH_PERFECT_CATHY = 1007, BB_ACH_PERFECT_ZOE = 1008, BB_ACH_PERFECT_GALE = 1009, BB_ACH_PERFECT_CLAIRE = 1010, BB_ACH_PERFECT_DAWNA = 1011,
    BB_ACH_PLAYMOD = 1015, BB_ACH_EYES_UP_HERE = 1016, BB_ACH_ZOE_EARLY_FAIL = 1017, BB_ACH_CHAD = 1018, BB_ACH_GIGACHAD = 1019,
    BB_ACH_NOSE_HONK_20 = 1020, BB_ACH_NOSE_HONK_50 = 1021, BB_ACH_NOSE_HONK_100 = 1022, BB_ACH_NICE = 1069, BB_ACH_IMPOSSIBLE = 1099
};

/* ---- end-of-run bookkeeping ---- */
typedef struct {
    const char* stage_id;
    int         level_enum;       /* meta.cfg level_enum */
    int         diff;             /* 0..2 */
    int         grade;            /* bb_grade_t */
    uint32_t    hit, registered;
    int32_t     score;
    uint32_t    max_combo;
    int         powerup;          /* active power-up item (0 none) */
    bool        clean;            /* not autoplay / not a test run */
    int64_t     fail_time_us;     /* chart time of failure (fail results only) */
    bool        mod;              /* an SD-card mod level: mod stats (804 / 805) and the flat mod EXP instead of the level rules */
} bb_run_t;

#define BB_RUN_MAX_NEW 12
typedef struct {
    int32_t  money_gained, exp_gained;
    int      rank_before, rank_after;
    int      new_ach[BB_RUN_MAX_NEW];   uint32_t new_ach_count;
    int      new_items[BB_RUN_MAX_NEW]; uint32_t new_item_count;   /* collectables / gallery images */
    bool     powerup_consumed;
} bb_run_outcome_t;

/* A level was passed: payout, EXP, stats, level/collectable/gallery unlocks, achievements (game.gd _on_level_passed + ScoreScreen).
 * The power-up is NOT consumed here: the original removes it on Continue (ScoreScreen -> game_powerup_manager), so Retry keeps it. */
void bb_run_passed(bb_save_t* s, const bb_run_t* run, bb_run_outcome_t* out);
/* Is a level open? Built-in levels (has_prereq): BaseLevelNode.gd, open when the level_prereq item is owned (0 = from the start);
 * SD mods: the pack's act order. The dev-build unlock-all opens everything. */
bool bb_level_open(const bb_save_t* s, bool has_prereq, int prereq, const char* act_id, int level_index);
/* A collectable was read in the Collection (collection_popup.gd LEVEL_UNLOCKS): Cathy's Dorm Key unlocks EGG_DORM_KEY_READ (901),
 * After Class's level_prereq. True when that unlocked something new. */
bool bb_collectable_read(bb_save_t* s, int item);
/* Continue after a pass: the active power-up is used up (true if it was owned). */
bool bb_powerup_consume(bb_save_t* s, int item);
/* Give Up on the level-failed screen: GameOver.gd takes 10 EXP. */
void bb_run_gave_up(bb_save_t* s);
#define BB_EXP_MOD_PASS 25   /* game.gd: a (non-legacy) mod pass gives 25 EXP */
/* A level was failed: stats + the "Seriously?" early-fail unlocks. */
void bb_run_failed(bb_save_t* s, const bb_run_t* run, bb_run_outcome_t* out);

#endif
