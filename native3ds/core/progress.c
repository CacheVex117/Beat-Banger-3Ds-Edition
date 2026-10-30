/**
 * @file progress.c
 * @brief Economy and meta-progression (see progress.h).
 */
#include "progress.h"
#include "rhythm_core.h"
#include <string.h>

static const char* const k_rank_title[BB_RANK_COUNT] = { "Virgin", "Newbie", "Amateur", "Rookie", "Professional", "Pornstar", "The Goat", "Beat Banger" };
static const int k_rank_exp[BB_RANK_COUNT] = { 0, 100, 200, 300, 500, 1000, 3000, 9999 };

const char* bb_rank_title(int rank) { return (rank >= 0 && rank < BB_RANK_COUNT) ? k_rank_title[rank] : "?"; }
int bb_rank_required_exp(int rank) { return (rank >= 0 && rank < BB_RANK_COUNT) ? k_rank_exp[rank] : 0; }

int bb_rank_from_exp(int exp) {
    if (exp >= BB_EXP_CAP) return BB_RANK_COUNT - 1;
    for (int i = 1; i < BB_RANK_COUNT; i++) {
        if (exp < k_rank_exp[i]) return i - 1;
    }
    return 0;
}

int bb_rank_next(int exp) {
    if (exp >= BB_EXP_CAP) return BB_RANK_COUNT - 1;
    for (int i = 1; i < BB_RANK_COUNT; i++) {
        if (exp < k_rank_exp[i]) return i;
    }
    return BB_RANK_COUNT - 1;
}

int32_t bb_exp_for_difficulty(int diff) {
    static const int32_t v[3] = { 50, 100, 150 };
    return (diff >= 0 && diff < 3) ? v[diff] : 0;
}

int32_t bb_money_payout(int32_t score) {
    if (score <= 0) return 0;
    return (int32_t)(((int64_t)score + 5) / 10); /* snappedi(score * 0.1, 1) */
}

void bb_give_money(bb_save_t* s, int32_t amount) {
    int64_t m = (int64_t)s->player.money + amount;
    s->player.money = (int32_t)(m < 0 ? 0 : (m > BB_MONEY_CAP ? BB_MONEY_CAP : m));
}

void bb_give_exp(bb_save_t* s, int32_t amount) {
    int64_t e = (int64_t)s->player.exp + amount;
    s->player.exp = (int32_t)(e < 0 ? 0 : (e > BB_EXP_CAP ? BB_EXP_CAP : e));
}

bool bb_item_owned(const bb_save_t* s, int item) { return bb_player_has_item(&s->player, item); }

bb_buy_result_t bb_shop_buy(bb_save_t* s, int item, int32_t price, int32_t required_exp) {
    if (bb_item_owned(s, item)) return BB_BUY_OWNED;
    if (s->player.exp < required_exp) return BB_BUY_RANK_LOCKED;
    if (s->player.money < price) return BB_BUY_NO_MONEY;
    if (!bb_player_add_item(&s->player, item)) return BB_BUY_OWNED;
    s->player.money -= price;
    return BB_BUY_OK;
}

bool bb_has_powerups(const bb_save_t* s) {
    for (int it = BB_ITEM_POWERUP_EXTRA_LIVES; it <= BB_ITEM_POWERUP_HARDCORE; it++) {
        if (bb_item_owned(s, it)) return true;
    }
    return false;
}

bb_powerup_effects_t bb_powerup_effects(int item) {
    bb_powerup_effects_t e = { 10, 1, 0, false, false, false };
    switch (item) {
        case BB_ITEM_POWERUP_EXTRA_LIVES:   e.extra_health = 3; break;
        case BB_ITEM_POWERUP_NO_FAIL:       e.score_mult_x10 = 5; e.no_fail = true; break;
        case BB_ITEM_POWERUP_DOUBLE_EDGED:  e.score_mult_x10 = 20; e.damage = 2; break;
        case BB_ITEM_POWERUP_BLINDFOLD:     e.score_mult_x10 = 30; e.disappearing = true; break;
        case BB_ITEM_POWERUP_HARDCORE:      e.score_mult_x10 = 40; e.hardcore = true; break;
        default: break;
    }
    return e;
}

const char* bb_powerup_title(int item) {
    switch (item) {
        case BB_ITEM_POWERUP_EXTRA_LIVES:   return "Performance Pills";
        case BB_ITEM_POWERUP_NO_FAIL:       return "No Fail Condom";
        case BB_ITEM_POWERUP_DOUBLE_EDGED:  return "Double Edged Sword";
        case BB_ITEM_POWERUP_BLINDFOLD:     return "The Blindfold";
        case BB_ITEM_POWERUP_HARDCORE:      return "Hardcore Lube";
        default:                            return "No Powerup";
    }
}

int bb_collectable_for_level(int level_enum) {
    switch (level_enum) {
        case 501: return 714;  /* Cathy Vol 1    -> Cathy's plushie */
        case 502: return 708;  /* Zoe Vol 1      -> Zoe's beanie */
        case 503: return 710;  /* Gale Vol 1     -> Gale's talisman */
        case 504: return 701;  /* Claire Vol 1   -> Claire's panties */
        case 505: return 706;  /* Dawna Vol 1    -> Dawna's earring */
        case 506: return 717;  /* Zoe Vol 2      -> Zoe's feather */
        case 507: return 723;  /* Cathy Vol 2    -> Cathy's dorm key */
        case 508: return 716;  /* Dawna Vol 2    -> Dawna's milk */
        case 509: return 722;  /* Gale Vol 2     -> Burger bull */
        case 510: return 704;  /* Claire Vol 2   -> Claire's vibrator */
        case 598: return 718;  /* Makayla        -> Makayla's hentai */
        case 599: return 705;  /* Jasmine        -> Jasmine skull sticker */
        default:  return 0;
    }
}

/* ---------------- achievements ---------------- */
#define OR 0xFFA500u   /* [color=orange] */
static const bb_ach_def_t k_achs[BB_ACH_COUNT] = {
    { 1001, "Taste Of Victory", "Beat a Level for the first time", 100, "first time", OR, "fingies", 100, true },
    { 1004, "Combo King", "Win with a Max Combo", 2000, "Max Combo", OR, "combo_king", 100, true },
    { 1015, "Something Different", "Play A Mod!", 200, "Mod!", OR, "gear", 100, false },
    { 1007, "Double Wide Surprise", "Get 6 Stars on Cathy's Level", 500, "Cathy's", OR, "cathy_bell", 100, true },
    { 1008, "Angry Bird", "Get 6 Stars on Zoe's Level", 500, "Zoe's", OR, "feather", 100, true },
    { 1009, "Not Bad For A Puppy", "Get 6 Stars on Gale's Level", 500, "Gale's", OR, "gale_perfect", 100, true },
    { 1010, "Upstanding Employee", "Get 6 Stars on The Boss", 500, "The Boss", OR, "mug", 100, true },
    { 1011, "Indulgence", "Get 6 Stars on Dawna's Level", 500, "Dawna's", OR, "dawna_perfect_candy", 100, true },
    { 1099, "Impossible!", "Win with Hardcore Lube active.", 10000, "Hardcore Lube", 0xFF0000u, "impossible", 100, true },
    { 1017, "Seriously?", "Fail Zoe's first level on virgin difficulty.", 1, "Zoe's", OR, "stopwatch", 100, true },
    { 1016, "Eyes Up Here", "I think you've stared long enough...", 500, "I think you've stared long enough...", 0xFFC0CBu, "staring_jasmine", 100, false },
    { 1069, "Sixty Nine", "Nice", 69, "", 0, "nice", 100, true },
    { 1018, "Chad", "Perfect clear 5 levels", 500, "", 0, "chad", 85, true },
    { 1019, "Gigachad", "Perfect clear 10 levels", 500, "", 0, "gigachad", 85, true },
    { 1020, "Honk If You're Horny", "Honk Jasmine's nose 20 times", 200, "Jasmine's nose", OR, "honk_1", 85, true },
    { 1021, "Serial Honker", "Honk Jasmine's nose 50 times", 500, "Jasmine's nose", OR, "honk_2", 85, true },
    { 1022, "Honk-a-holic", "Honk Jasmine's nose 100 times", 1000, "Jasmine's nose", OR, "honk_3", 85, true },
};
#undef OR
/* class/ach.gd entries with no tile in the grid (nothing in the game awards them); kept so bb_ach_find knows every enum. */
static const bb_ach_def_t k_ach_unused[] = {
    { 1002, "New Beginnings", "", 100, "", 0, "trophy", 100, false },
    { 1003, "New Man", "", 500, "", 0, "trophy", 100, false },
    { 1005, "Perfectionist", "", 3000, "", 0, "trophy", 100, false },
    { 1006, "Hardcore Hero", "", 4000, "", 0, "trophy", 100, false },
    { 1012, "Clean Up Crew", "", 2000, "", 0, "trophy", 100, false },
    { 1013, "Collector", "", 5000, "", 0, "trophy", 100, false },
};
#define ACH_UNUSED (int)(sizeof(k_ach_unused) / sizeof(k_ach_unused[0]))

const bb_ach_def_t* bb_ach_defs(void) { return k_achs; }

const bb_ach_def_t* bb_ach_find(int id) {
    for (int i = 0; i < BB_ACH_COUNT; i++) if (k_achs[i].id == id) return &k_achs[i];
    for (int i = 0; i < ACH_UNUSED; i++) if (k_ach_unused[i].id == id) return &k_ach_unused[i];
    return NULL;
}

const char* bb_ach_icon_name(int id) {
    const bb_ach_def_t* d = bb_ach_find(id);
    return d && d->icon ? d->icon : "trophy";
}

/* class/ach.gd "stat" / "required_value" */
static int ach_stat(int id, int32_t* need) {
    switch (id) {
        case BB_ACH_TASTEOFVICTORY: *need = 1; return BB_STAT_LEVELS_BEATEN;
        case BB_ACH_NOSE_HONK_20: *need = 20; return BB_STAT_NOSE_HONKS;
        case BB_ACH_NOSE_HONK_50: *need = 50; return BB_STAT_NOSE_HONKS;
        case BB_ACH_NOSE_HONK_100: *need = 100; return BB_STAT_NOSE_HONKS;
        default: *need = 1; return -1;
    }
}

void bb_ach_progress(const bb_save_t* s, int id, int32_t* value, int32_t* max) {
    int32_t need;
    const int st = ach_stat(id, &need);
    *max = need;
    *value = st < 0 ? (bb_player_has_ach(&s->player, id) ? 1 : 0) : s->player.stats[st];
}

bool bb_ach_unlocked(const bb_save_t* s, int id) { return bb_player_has_ach(&s->player, id); }

bool bb_ach_unlock(bb_save_t* s, int id) {
    const bb_ach_def_t* d = bb_ach_find(id);
    if (!d || !bb_player_add_ach(&s->player, id)) {
        return false;
    }
    bb_give_money(s, d->reward);   /* Items.unlock_achievement: reward money + 10 EXP */
    bb_give_exp(s, 10);
    return true;
}

int bb_ach_check_stats(bb_save_t* s, int* new_ids, int max) {
    int n = 0;
    for (int i = 0; i < BB_ACH_COUNT; i++) {
        int32_t need;
        const int st = ach_stat(k_achs[i].id, &need);
        if (st < 0 || s->player.stats[st] < need) continue;
        if (bb_ach_unlock(s, k_achs[i].id) && new_ids && n < max) new_ids[n++] = k_achs[i].id;
    }
    return n;
}

/* ---------------- end-of-run bookkeeping ---------------- */
static void note_ach(bb_save_t* s, bb_run_outcome_t* out, int id) {
    if (bb_ach_unlock(s, id) && out->new_ach_count < BB_RUN_MAX_NEW) {
        out->new_ach[out->new_ach_count++] = id;
    }
}

static void note_item(bb_save_t* s, bb_run_outcome_t* out, int item) {
    if (item > 0 && bb_player_add_item(&s->player, item) && out->new_item_count < BB_RUN_MAX_NEW) {
        out->new_items[out->new_item_count++] = item;
    }
}

void bb_run_passed(bb_save_t* s, const bb_run_t* run, bb_run_outcome_t* out) {
    memset(out, 0, sizeof(*out));
    out->rank_before = bb_rank_from_exp(s->player.exp);
    int32_t money0 = s->player.money, exp0 = s->player.exp;

    bb_player_stat_add(&s->player, run->mod ? BB_STAT_MODS_BEATEN : BB_STAT_LEVELS_BEATEN, 1);   /* ScoreScreen: STAT_MODS_BEATEN / STAT_LEVELS_BEATEN */
    if (run->clean) {
        bb_player_stat_add(&s->player, BB_STAT_HIT_NOTES, (int32_t)run->hit);
        bb_player_stat_add(&s->player, BB_STAT_MISSED_NOTES, (int32_t)(run->registered - run->hit));
        bb_player_stat_max(&s->player, BB_STAT_BEST_COMBO, (int32_t)run->max_combo);
        bool perfect = run->registered > 0 && run->hit >= run->registered;
        if (perfect) bb_player_stat_add(&s->player, BB_STAT_PERFECT_CLEARS, 1);   /* Items.incriment_stats */

        bb_give_money(s, bb_money_payout(run->score));                              /* Save.score_payout */
        if (run->powerup != BB_ITEM_POWERUP_NO_FAIL) {   /* the No Fail Condom withholds the EXP (game.gd) */
            bb_give_exp(s, run->mod ? BB_EXP_MOD_PASS : bb_exp_for_difficulty(run->diff));   /* mods: 25; levels: Save.give_exp_by_difficulty */
        }
        if (run->level_enum > 0) {
            bb_player_add_item(&s->player, run->level_enum);                          /* Save.unlock_level -> silent_unlock_item */
            note_item(s, out, bb_collectable_for_level(run->level_enum));
        }

        /* achievement_check.gd */
        if (perfect) {
            note_ach(s, out, BB_ACH_COMBOKING);
            if (s->player.stats[BB_STAT_PERFECT_CLEARS] >= 5) note_ach(s, out, BB_ACH_CHAD);
            if (s->player.stats[BB_STAT_PERFECT_CLEARS] >= 10) note_ach(s, out, BB_ACH_GIGACHAD);
        }
        if (run->registered > 0 && (run->hit * 100u) / run->registered == 69u) note_ach(s, out, BB_ACH_NICE);
        if (run->grade >= BB_GRADE_S && run->stage_id) {
            if (strcmp(run->stage_id, "cathy_e1") == 0) note_ach(s, out, BB_ACH_PERFECT_CATHY);
            if (strcmp(run->stage_id, "zoe_e1") == 0) note_ach(s, out, BB_ACH_PERFECT_ZOE);
            if (strcmp(run->stage_id, "gale_e1") == 0) note_ach(s, out, BB_ACH_PERFECT_GALE);
            if (strcmp(run->stage_id, "claire_e1") == 0) note_ach(s, out, BB_ACH_PERFECT_CLAIRE);
            if (strcmp(run->stage_id, "dawna_e1") == 0) note_ach(s, out, BB_ACH_PERFECT_DAWNA);
        }
        if (run->diff == 2 && run->stage_id) {   /* expert clears unlock gallery photos */
            if (strcmp(run->stage_id, "cathy_e1") == 0) note_item(s, out, BB_ITEM_GALLERY_CATHY_PANTS);
            if (strcmp(run->stage_id, "gale_e1") == 0) note_item(s, out, BB_ITEM_GALLERY_GALE_BREAST_EXPAND);
            if (strcmp(run->stage_id, "claire_e1") == 0) note_item(s, out, BB_ITEM_GALLERY_CLAIRE_PILEDRIVER);
            if (strcmp(run->stage_id, "zoe_e2") == 0) note_item(s, out, BB_ITEM_GALLERY_FRENEMIES);     /* achievement_check.gd */
            if (strcmp(run->stage_id, "cathy_e2") == 0) note_item(s, out, BB_ITEM_GALLERY_STUDIO_TOUR);
        }
        if (run->powerup == BB_ITEM_POWERUP_HARDCORE) note_ach(s, out, BB_ACH_IMPOSSIBLE);
        if (s->player.stats[BB_STAT_LEVELS_BEATEN] >= 1) note_ach(s, out, BB_ACH_TASTEOFVICTORY);
    }

    out->money_gained = s->player.money - money0;
    out->exp_gained = s->player.exp - exp0;
    out->rank_after = bb_rank_from_exp(s->player.exp);
}

bool bb_level_open(const bb_save_t* s, bool has_prereq, int prereq, const char* act_id, int level_index) {
    if (s->settings.unlock_all) return true;
    if (has_prereq) return prereq <= 0 || bb_player_has_item(&s->player, prereq);
    return bb_save_level_unlocked(s, act_id, (uint8_t)level_index);
}

bool bb_collectable_read(bb_save_t* s, int item) {
    if (item == BB_ITEM_COLLECT_DORM_KEY) return bb_player_add_item(&s->player, BB_ITEM_EGG_DORM_KEY_READ);
    return false;
}

bool bb_powerup_consume(bb_save_t* s, int item) {
    s->player.equipped_powerup = 0;
    return item > 0 && bb_player_remove_item(&s->player, item);   /* PowerUpMananger.remove_powerups */
}

void bb_run_gave_up(bb_save_t* s) {
    bb_give_exp(s, -10);   /* GameOver.gd _on_giveup_button_up: Save.give_exp(-10) */
}

void bb_run_failed(bb_save_t* s, const bb_run_t* run, bb_run_outcome_t* out) {
    memset(out, 0, sizeof(*out));
    out->rank_before = out->rank_after = bb_rank_from_exp(s->player.exp);
    bb_player_stat_add(&s->player, run->mod ? BB_STAT_MODS_FAILED : BB_STAT_LEVELS_FAILED, 1);   /* GameOver.gd */
    if (run->clean) {
        bb_player_stat_add(&s->player, BB_STAT_HIT_NOTES, (int32_t)run->hit);
        bb_player_stat_add(&s->player, BB_STAT_MISSED_NOTES, (int32_t)(run->registered - run->hit));
    }
    /* achievement_check.level_premature: Zoe E1, Virgin, failed within the first 5 s */
    if (run->stage_id && strcmp(run->stage_id, "zoe_e1") == 0 && run->diff == 0 && run->fail_time_us <= 5000000) {
        note_item(s, out, BB_ITEM_GALLERY_ZOE_SERIOUSLY);
        note_ach(s, out, BB_ACH_ZOE_EARLY_FAIL);
    }
    /* failed runs keep the power-up (only a clear consumes it) */
}
