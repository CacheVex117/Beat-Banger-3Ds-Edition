/**
 * @file test_progress.c
 * @brief Host tests for core/progress.c (economy, ranks, shop, power-ups, achievements) and the v2 save image.
 */
#include <stdio.h>
#include <string.h>
#include "save_data.h"
#include "progress.h"
#include "rhythm_core.h"
#include "mod_gauge.h"

static int g_run = 0, g_failed = 0;
#define CHECK(cond, msg) do { g_run++; if (!(cond)) { g_failed++; fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); } } while (0)

static void test_ranks_and_rewards(void) {
    bb_save_t s;
    bb_save_defaults(&s);
    CHECK(s.player.money == 500 && s.player.exp == 0 && s.player.item_count == 0, "fresh player: $500, 0 EXP, nothing owned (SaveManager defaults)");
    s.player.money = 0;
    CHECK(bb_rank_from_exp(0) == 0 && bb_rank_from_exp(99) == 0 && bb_rank_from_exp(100) == 1, "rank boundaries");
    CHECK(bb_rank_from_exp(499) == 3 && bb_rank_from_exp(500) == 4 && bb_rank_from_exp(2999) == 5 && bb_rank_from_exp(3000) == 6, "rank mid");
    CHECK(bb_rank_from_exp(9998) == 6 && bb_rank_from_exp(9999) == 7, "top rank only at the cap");
    CHECK(bb_rank_next(0) == 1 && bb_rank_next(9999) == 7, "next rank");
    CHECK(bb_exp_for_difficulty(0) == 50 && bb_exp_for_difficulty(1) == 100 && bb_exp_for_difficulty(2) == 150, "exp per difficulty");
    CHECK(bb_money_payout(25844) == 2584 && bb_money_payout(25) == 3 && bb_money_payout(24) == 2 && bb_money_payout(-50) == 0, "payout = round(score*0.1)");
    bb_give_money(&s, 99990);
    bb_give_money(&s, 50);
    CHECK(s.player.money == BB_MONEY_CAP, "money cap");
    bb_give_money(&s, -200000);
    CHECK(s.player.money == 0, "money floor");
    bb_give_exp(&s, 20000);
    CHECK(s.player.exp == BB_EXP_CAP, "exp cap");
}

static void test_shop_and_powerups(void) {
    bb_save_t s;
    bb_save_defaults(&s);
    s.player.money = 0;   /* (a fresh save starts with $500; these checks count from zero) */
    bb_give_money(&s, 600);
    CHECK(bb_shop_buy(&s, BB_ITEM_POWERUP_NO_FAIL, 500, 200) == BB_BUY_RANK_LOCKED, "needs EXP 200");
    CHECK(bb_shop_buy(&s, BB_ITEM_POWERUP_EXTRA_LIVES, 69, 0) == BB_BUY_OK && s.player.money == 531, "buy pills");
    CHECK(bb_shop_buy(&s, BB_ITEM_POWERUP_EXTRA_LIVES, 69, 0) == BB_BUY_OWNED, "already owned");
    bb_give_exp(&s, 200);
    CHECK(bb_shop_buy(&s, BB_ITEM_POWERUP_HARDCORE, 1500, 0) == BB_BUY_NO_MONEY, "not enough money");
    CHECK(bb_shop_buy(&s, BB_ITEM_POWERUP_NO_FAIL, 500, 200) == BB_BUY_OK && s.player.money == 31, "buy no-fail");
    CHECK(bb_has_powerups(&s), "has power-ups");

    bb_powerup_effects_t e = bb_powerup_effects(BB_ITEM_POWERUP_HARDCORE);
    CHECK(e.hardcore && e.score_mult_x10 == 40 && e.damage == 1, "hardcore 4x");
    e = bb_powerup_effects(BB_ITEM_POWERUP_DOUBLE_EDGED);
    CHECK(e.score_mult_x10 == 20 && e.damage == 2, "double edged");
    e = bb_powerup_effects(BB_ITEM_POWERUP_BLINDFOLD);
    CHECK(e.score_mult_x10 == 30 && e.disappearing, "blindfold 3x");
    e = bb_powerup_effects(BB_ITEM_POWERUP_NO_FAIL);
    CHECK(e.score_mult_x10 == 5 && e.no_fail, "no fail 0.5x");
    e = bb_powerup_effects(BB_ITEM_POWERUP_EXTRA_LIVES);
    CHECK(e.extra_health == 3 && e.score_mult_x10 == 10, "pills");
    e = bb_powerup_effects(0);
    CHECK(e.score_mult_x10 == 10 && e.damage == 1 && !e.hardcore && !e.no_fail, "no power-up");
}

static void test_runs(void) {
    bb_save_t s;
    bb_run_outcome_t out;
    bb_save_defaults(&s);
    bb_player_add_item(&s.player, BB_ITEM_POWERUP_EXTRA_LIVES);
    s.player.equipped_powerup = BB_ITEM_POWERUP_EXTRA_LIVES;
    bb_run_t run = { "cathy_e1", 501, 2, BB_GRADE_SS, 569, 569, 58630, 569, BB_ITEM_POWERUP_EXTRA_LIVES, true, 0 };
    bb_run_passed(&s, &run, &out);
    CHECK(s.player.stats[BB_STAT_LEVELS_BEATEN] == 1 && s.player.stats[BB_STAT_PERFECT_CLEARS] == 1, "stats updated");
    CHECK(bb_player_has_item(&s.player, 501), "level enum unlocked");
    CHECK(bb_player_has_item(&s.player, 714), "collectable unlocked by beating Virgin Newbie");
    CHECK(bb_player_has_item(&s.player, BB_ITEM_GALLERY_CATHY_PANTS), "expert clear unlocks photo 301");
    CHECK(bb_ach_unlocked(&s, BB_ACH_COMBOKING) && bb_ach_unlocked(&s, BB_ACH_PERFECT_CATHY) && bb_ach_unlocked(&s, BB_ACH_TASTEOFVICTORY), "achievements unlocked");
    CHECK(bb_item_owned(&s, BB_ITEM_POWERUP_EXTRA_LIVES), "a pass keeps the power-up until Continue (Retry is free)");
    CHECK(bb_powerup_consume(&s, BB_ITEM_POWERUP_EXTRA_LIVES) && !bb_item_owned(&s, BB_ITEM_POWERUP_EXTRA_LIVES) && s.player.equipped_powerup == 0,
          "Continue consumes the power-up");
    CHECK(!bb_powerup_consume(&s, BB_ITEM_POWERUP_EXTRA_LIVES), "a power-up is consumed once");
    CHECK(s.player.money == 500 + 5863 + 2000 + 500 + 100, "money: start + payout plus achievement rewards");
    CHECK(s.player.exp == 150 + 30, "exp: difficulty reward plus 10 per achievement");
    CHECK(bb_rank_from_exp(s.player.exp) == 1, "rank from exp");
    CHECK(out.rank_before == 0 && out.rank_after == 1, "rank-up reported");
    bb_run_passed(&s, &run, &out);
    CHECK(out.new_ach_count == 0 && out.money_gained == 5863, "no duplicate achievement rewards");

    bb_save_defaults(&s);
    bb_run_t nf = { "gale_e1", 503, 1, BB_GRADE_D, 69, 100, 1000, 10, BB_ITEM_POWERUP_NO_FAIL, true, 0 };
    bb_run_passed(&s, &nf, &out);
    CHECK(s.player.exp == (int32_t)(10 * out.new_ach_count) && bb_ach_unlocked(&s, BB_ACH_NICE), "no-fail: no difficulty EXP; exactly 69 percent");
    bb_save_defaults(&s);
    bb_run_t hc = { "dawna_e1", 505, 0, BB_GRADE_S, 100, 100, 5000, 100, BB_ITEM_POWERUP_HARDCORE, true, 0 };
    bb_run_passed(&s, &hc, &out);
    CHECK(bb_ach_unlocked(&s, BB_ACH_IMPOSSIBLE) && bb_ach_unlocked(&s, BB_ACH_PERFECT_DAWNA), "hardcore clear");
    bb_save_defaults(&s);
    bb_run_t cheat = { "cathy_e1", 501, 2, BB_GRADE_SS, 10, 10, 9999, 10, 0, false, 0 };
    bb_run_passed(&s, &cheat, &out);
    CHECK(s.player.money == 500 && s.player.exp == 0 && out.new_ach_count == 0, "autoplay earns nothing");

    bb_save_defaults(&s);
    bb_run_t pc = { "gale_e1", 503, 0, BB_GRADE_SS, 10, 10, 100, 10, 0, true, 0 };
    for (int i = 0; i < 4; i++) bb_run_passed(&s, &pc, &out);
    CHECK(!bb_ach_unlocked(&s, BB_ACH_CHAD), "4 perfect clears: no Chad");
    bb_run_passed(&s, &pc, &out);
    CHECK(bb_ach_unlocked(&s, BB_ACH_CHAD) && !bb_ach_unlocked(&s, BB_ACH_GIGACHAD), "5th perfect clear: Chad");
    for (int i = 0; i < 5; i++) bb_run_passed(&s, &pc, &out);
    CHECK(bb_ach_unlocked(&s, BB_ACH_GIGACHAD), "10 perfect clears: Giga Chad");

    bb_save_defaults(&s);
    bb_run_t fl = { "zoe_e1", 502, 0, 0, 3, 10, 0, 1, 0, true, 4000000 };
    bb_run_failed(&s, &fl, &out);
    CHECK(s.player.stats[BB_STAT_LEVELS_FAILED] == 1 && bb_ach_unlocked(&s, BB_ACH_ZOE_EARLY_FAIL) && bb_player_has_item(&s.player, BB_ITEM_GALLERY_ZOE_SERIOUSLY), "Zoe early fail");
    bb_save_defaults(&s);
    fl.fail_time_us = 5000001;
    bb_run_failed(&s, &fl, &out);
    CHECK(!bb_ach_unlocked(&s, BB_ACH_ZOE_EARLY_FAIL), "a late fail is not the early-fail achievement");
    /* a failed run keeps the power-up */
    bb_save_defaults(&s);
    bb_player_add_item(&s.player, BB_ITEM_POWERUP_BLINDFOLD);
    bb_run_t fp = { "cathy_e1", 501, 0, 0, 1, 10, 0, 1, BB_ITEM_POWERUP_BLINDFOLD, true, 9000000 };
    bb_run_failed(&s, &fp, &out);
    CHECK(bb_item_owned(&s, BB_ITEM_POWERUP_BLINDFOLD), "failed run does not consume the power-up");
}

/* The Achievements grid (achievements.tscn): 17 tiles in its order, each with its own icon; stat-tracked unlocks (class/ach.gd) */
static void test_achievement_grid(void) {
    static const int order[BB_ACH_COUNT] = { 1001, 1004, 1015, 1007, 1008, 1009, 1010, 1011, 1099, 1017, 1016, 1069, 1018, 1019, 1020, 1021, 1022 };
    const bb_ach_def_t* d = bb_ach_defs();
    const int count = (int)(sizeof(order) / sizeof(order[0]));
    CHECK(count == 17, "17 achievement tiles");
    bool ok = true, icons_unique = true;
    for (int i = 0; i < BB_ACH_COUNT; i++) {
        if (d[i].id != order[i] || !d[i].name[0] || !d[i].desc[0] || !d[i].icon || strcmp(d[i].icon, "trophy") == 0) ok = false;
        if (d[i].hl[0] && !strstr(d[i].desc, d[i].hl)) ok = false;
        for (int k = 0; k < i; k++) if (strcmp(d[i].icon, d[k].icon) == 0) icons_unique = false;
    }
    CHECK(ok, "grid order, titles, descriptions, highlight spans, icons");
    CHECK(icons_unique, "every tile has its own icon");
    CHECK(strcmp(bb_ach_icon_name(BB_ACH_ZOE_EARLY_FAIL), "stopwatch") == 0 && strcmp(bb_ach_icon_name(4242), "trophy") == 0, "icon lookup");
    CHECK(bb_ach_find(BB_ACH_CHAD)->icon_pct == 85 && bb_ach_find(BB_ACH_NOSE_HONK_100)->icon_pct == 85 && bb_ach_find(BB_ACH_NICE)->icon_pct == 100,
          "icon_scale_multiplier 0.85 on Chad, Gigachad and the honks");
    CHECK(strcmp(bb_ach_find(BB_ACH_NICE)->name, "Sixty Nine") == 0 && strcmp(bb_ach_find(BB_ACH_GIGACHAD)->name, "Gigachad") == 0, "grid titles (locale)");
    CHECK(bb_ach_find(1005) != NULL && !bb_ach_find(1005)->evaluable, "unused ach.gd entries still known");

    bb_save_t s;
    int ids[8];
    int32_t v, mx;
    bb_save_defaults(&s);
    s.player.money = 0;
    s.player.stats[BB_STAT_NOSE_HONKS] = 49;
    bb_ach_progress(&s, BB_ACH_NOSE_HONK_50, &v, &mx);
    CHECK(v == 49 && mx == 50, "honk progress 49 / 50");
    int n = bb_ach_check_stats(&s, ids, 8);
    CHECK(n == 1 && ids[0] == BB_ACH_NOSE_HONK_20 && !bb_ach_unlocked(&s, BB_ACH_NOSE_HONK_50), "49 honks: only the first honk achievement");
    CHECK(s.player.money == 200 && s.player.exp == 10, "honk reward 200 + 10 EXP");
    s.player.stats[BB_STAT_NOSE_HONKS] = 100;
    n = bb_ach_check_stats(&s, ids, 8);
    CHECK(n == 2 && bb_ach_unlocked(&s, BB_ACH_NOSE_HONK_100), "100 honks: the other two");
    CHECK(bb_ach_check_stats(&s, ids, 8) == 0, "no repeats");
    bb_ach_progress(&s, BB_ACH_CHAD, &v, &mx);
    CHECK(v == 0 && mx == 1, "no-stat progress is 0 / 1 while locked");
}

static void test_save_v2(void) {
    bb_save_t s, b;
    uint8_t buf[BB_SAVE_MAX_BYTES];
    memset(&b, 0, sizeof(b));
    bb_save_defaults(&s);
    s.player.money = 0;
    bb_give_money(&s, 1234);
    bb_give_exp(&s, 321);
    bb_player_add_item(&s.player, 714);
    bb_player_add_item(&s.player, 203);
    bb_player_add_ach(&s.player, BB_ACH_NICE);
    s.player.equipped_powerup = 104;
    s.player.menu_music = 203;
    s.player.outfit = 1104;
    s.player.stats[BB_STAT_BEST_COMBO] = 569;
    size_t n = bb_save_serialize(&s, buf, sizeof(buf));
    CHECK(n > 0 && bb_save_deserialize(&b, buf, n), "v2 roundtrip");
    CHECK(b.player.money == 1234 && b.player.exp == 321 && b.player.item_count == 2 && b.player.equipped_powerup == 104 &&
          b.player.menu_music == 203 && b.player.outfit == 1104 && b.player.stats[BB_STAT_BEST_COMBO] == 569 && bb_player_has_ach(&b.player, BB_ACH_NICE),
          "player block roundtrip");

    /* a version-1 image (no player block) still loads with a fresh player */
    bb_save_t v1;
    bb_save_defaults(&v1);
    v1.settings.scroll_idx = 3;
    uint8_t img[BB_SAVE_MAX_BYTES];
    size_t full = bb_save_serialize(&v1, img, sizeof(img));
    size_t cut = 16 + 7 + 4 + 4;   /* header + v1 settings + empty stage and act tables */
    CHECK(full > cut, "image has a player block to strip");
    uint8_t v1img[BB_SAVE_MAX_BYTES];
    memcpy(v1img, img, 16 + 7);                      /* v3/v4 settings carry extra bytes (volumes, cutscene toggle): drop them for v1 */
    memcpy(v1img + 16 + 7, img + 16 + 10, 8);
    v1img[4] = 1;
    uint32_t plen = (uint32_t)(cut - 16);
    v1img[8] = (uint8_t)plen; v1img[9] = (uint8_t)(plen >> 8); v1img[10] = 0; v1img[11] = 0;
    uint32_t crc = bb_crc32(v1img + 16, plen);
    v1img[12] = (uint8_t)crc; v1img[13] = (uint8_t)(crc >> 8); v1img[14] = (uint8_t)(crc >> 16); v1img[15] = (uint8_t)(crc >> 24);
    CHECK(bb_save_deserialize(&b, v1img, cut) && b.settings.scroll_idx == 3 && b.player.money == 500, "v1 image loads with a default player ($500)");

    /* v5 round-trips reduce_motion; a v3 image (no cutscene toggle / reduce motion) and a v4 image (no reduce motion) still load */
    bb_save_t v5;
    bb_save_defaults(&v5);
    v5.settings.skip_cutscene = 1;
    v5.settings.reduce_motion = 1;
    v5.settings.master_vol = 7;
    v5.settings.scroll_mode = BB_SCROLL_SIDE;
    v5.settings.theme = BB_THEME_HALLOWEEN;
    size_t n7 = bb_save_serialize(&v5, img, sizeof(img));
    CHECK(bb_save_deserialize(&b, img, n7) && b.settings.theme == BB_THEME_HALLOWEEN && b.settings.scroll_mode == BB_SCROLL_SIDE, "v7 theme roundtrip");
    {   /* an out-of-range theme is rejected even with a valid crc */
        bb_save_t bad7 = v5;
        bad7.settings.theme = 9;
        uint8_t badimg[BB_SAVE_MAX_BYTES];
        size_t nb = bb_save_serialize(&bad7, badimg, sizeof(badimg));
        CHECK(!bb_save_deserialize(&b, badimg, nb), "theme out of range rejected");
    }
    /* the v6 image (no theme byte): locate the byte by serializing with the theme off, drop it, version 6, fix size + crc */
    size_t n6;
    {
        uint8_t off7[BB_SAVE_MAX_BYTES];
        bb_save_t v7off = v5;
        v7off.settings.theme = 0;
        bb_save_serialize(&v7off, off7, sizeof(off7));
        size_t ti = 16;
        while (ti < n7 && img[ti] == off7[ti]) ti++;
        memmove(img + ti, img + ti + 1, n7 - ti - 1);
        n6 = n7 - 1;
        img[4] = 6;
        uint32_t plen6 = (uint32_t)(n6 - 16);
        img[8] = (uint8_t)plen6; img[9] = (uint8_t)(plen6 >> 8); img[10] = (uint8_t)(plen6 >> 16); img[11] = 0;
        uint32_t crc6 = bb_crc32(img + 16, plen6);
        img[12] = (uint8_t)crc6; img[13] = (uint8_t)(crc6 >> 8); img[14] = (uint8_t)(crc6 >> 16); img[15] = (uint8_t)(crc6 >> 24);
    }
    CHECK(bb_save_deserialize(&b, img, n6) && b.settings.scroll_mode == BB_SCROLL_SIDE && b.settings.reduce_motion == 1 &&
          b.settings.theme == BB_THEME_DEFAULT, "v6 image loads with the default theme");
    {   /* an out-of-range scroll mode is rejected even with a valid crc */
        bb_save_t bad6 = v5;
        bad6.settings.scroll_mode = 7;
        uint8_t badimg[BB_SAVE_MAX_BYTES];
        size_t nb = bb_save_serialize(&bad6, badimg, sizeof(badimg));
        CHECK(!bb_save_deserialize(&b, badimg, nb), "scroll mode out of range rejected");
    }
    /* build the v5 image (no scroll-mode byte) the older-version checks below start from */
    size_t n5;
    {
        uint8_t off6[BB_SAVE_MAX_BYTES];
        bb_save_t v6off = v5;
        v6off.settings.scroll_mode = 0;
        v6off.settings.theme = 0;
        bb_save_serialize(&v6off, off6, sizeof(off6));
        size_t smi = 16;
        while (smi < n6 && img[smi] == off6[smi]) smi++;
        memmove(img + smi, img + smi + 1, n6 - smi - 1);
        n5 = n6 - 1;
        img[4] = 5;
        uint32_t plen5 = (uint32_t)(n5 - 16);
        img[8] = (uint8_t)plen5; img[9] = (uint8_t)(plen5 >> 8); img[10] = (uint8_t)(plen5 >> 16); img[11] = 0;
        uint32_t crc5 = bb_crc32(img + 16, plen5);
        img[12] = (uint8_t)crc5; img[13] = (uint8_t)(crc5 >> 8); img[14] = (uint8_t)(crc5 >> 16); img[15] = (uint8_t)(crc5 >> 24);
    }
    CHECK(bb_save_deserialize(&b, img, n5) && b.settings.skip_cutscene == 1 && b.settings.reduce_motion == 1 && b.settings.master_vol == 7 &&
          b.settings.scroll_mode == BB_SCROLL_DOWN, "v5 image loads with downscroll");
    /* locate the reduce_motion byte by serializing with it on and off */
    uint8_t imgoff[BB_SAVE_MAX_BYTES];
    memcpy(imgoff, img, n5);
    {   /* the same v5 image with reduce_motion off: its byte is the one just before the stage count */
        size_t k = 16 + 1 + 2 + 1 + 1 + 1 + 1 + 1 + 1 + 1;   /* control, calibration, scroll, music, sfx, unlock_all, master, voice, skip */
        imgoff[k] = 0;
    }
    size_t rmi = 16;
    while (rmi < n5 && img[rmi] == imgoff[rmi]) rmi++;
    uint8_t v4img[BB_SAVE_MAX_BYTES];
    memcpy(v4img, img, rmi);   /* everything up to and including skip_cutscene */
    memcpy(v4img + rmi, img + rmi + 1, n5 - rmi - 1);
    size_t n4 = n5 - 1;
    v4img[4] = 4;
    uint32_t plen4 = (uint32_t)(n4 - 16);
    v4img[8] = (uint8_t)plen4; v4img[9] = (uint8_t)(plen4 >> 8); v4img[10] = (uint8_t)(plen4 >> 16); v4img[11] = 0;
    uint32_t crc4 = bb_crc32(v4img + 16, plen4);
    v4img[12] = (uint8_t)crc4; v4img[13] = (uint8_t)(crc4 >> 8); v4img[14] = (uint8_t)(crc4 >> 16); v4img[15] = (uint8_t)(crc4 >> 24);
    bool ok4 = bb_save_deserialize(&b, v4img, n4);
    CHECK(ok4, "v4 image deserializes");
    CHECK(b.settings.skip_cutscene == 1, "v4 image keeps skip_cutscene");
    CHECK(b.settings.reduce_motion == 0, "v4 image loads with reduce motion off");
    uint8_t v3img[BB_SAVE_MAX_BYTES];
    memcpy(v3img, img, rmi - 1);   /* up to voice_vol */
    memcpy(v3img + rmi - 1, img + rmi + 1, n5 - rmi - 1);
    size_t n3 = n5 - 2;
    v3img[4] = 3;
    uint32_t plen3 = (uint32_t)(n3 - 16);
    v3img[8] = (uint8_t)plen3; v3img[9] = (uint8_t)(plen3 >> 8); v3img[10] = (uint8_t)(plen3 >> 16); v3img[11] = 0;
    uint32_t crc3 = bb_crc32(v3img + 16, plen3);
    v3img[12] = (uint8_t)crc3; v3img[13] = (uint8_t)(crc3 >> 8); v3img[14] = (uint8_t)(crc3 >> 16); v3img[15] = (uint8_t)(crc3 >> 24);
    CHECK(bb_save_deserialize(&b, v3img, n3) && b.settings.skip_cutscene == 0 && b.settings.reduce_motion == 0 && b.settings.master_vol == 7, "v3 image loads with cutscenes on");

    bb_save_t evil = s;
    evil.player.money = 123456;
    n = bb_save_serialize(&evil, buf, sizeof(buf));
    CHECK(!bb_save_deserialize(&b, buf, n), "money over the cap is rejected despite a valid crc");

    bb_save_defaults(&s);
    for (int i = 0; i < BB_MAX_ITEMS + 5; i++) bb_player_add_item(&s.player, 2000 + i);
    CHECK(s.player.item_count == BB_MAX_ITEMS, "item table bounded");
    CHECK(bb_player_remove_item(&s.player, 2000) && s.player.item_count == BB_MAX_ITEMS - 1 && !bb_player_has_item(&s.player, 2000), "remove item");
    n = bb_save_serialize(&s, buf, sizeof(buf));
    CHECK(n > 0 && n < BB_SAVE_MAX_BYTES && bb_save_deserialize(&b, buf, n) && b.player.item_count == BB_MAX_ITEMS - 1, "full item table roundtrips within the size limit");
}

/* Regression: SD mods record into the same stage / act tables; they must never take the slots a built-in volume still needs. */
static void test_mod_records_never_block_builtin(void) {
    static const char* const bstages[] = { "cathy_e1", "zoe_e1", "gale_e1", "claire_e1", "dawna_e1", "cathy_e2", "zoe_e2", "makayla" };
    static const char* const bacts[] = { "bb_vol_1", "bb_vol_2", "bb_extras" };
    bb_save_t s;
    bb_save_defaults(&s);
    s.settings.unlock_all = 0;
    char id[24];
    int mods = 0;
    for (int k = 0; k < 200; k++) {   /* play far more mods than the tables hold */
        if (!bb_save_room_for_extra(&s, bstages, 8, bacts, 3)) continue;
        snprintf(id, sizeof(id), "mod_%03d", k);
        bb_save_record_result(&s, id, 0, 1000, 3, 10);
        snprintf(id, sizeof(id), "pack_%03d", k);
        bb_save_complete_level(&s, id, 0);
        mods++;
    }
    CHECK(mods > 0 && mods < 200, "mods record until only the built-in reserve is left");
    CHECK(s.act_count == BB_SAVE_MAX_ACTS - 3, "exactly the built-in act reserve stays free");
    bb_save_complete_level(&s, "bb_vol_1", 0);
    CHECK(bb_save_level_unlocked(&s, "bb_vol_1", 1), "a built-in clear still unlocks the next level with the tables full of mods");
    for (int i = 0; i < 8; i++) bb_save_record_result(&s, bstages[i], 1, 5000, 4, 50);
    for (int i = 0; i < 8; i++) CHECK(bb_save_find_stage(&s, bstages[i]) != NULL, "every built-in stage can record its score");
    CHECK(!bb_save_room_for_extra(&s, bstages, 8, bacts, 3), "no room is reported once the act reserve is used too");

    /* the raised limits still fit the save image: every stage and act table full, every item and achievement slot used */
    bb_save_t full, back;
    bb_save_defaults(&full);
    for (int k = 0; k < BB_SAVE_MAX_STAGES; k++) { snprintf(id, sizeof(id), "stage_%02d_xxxxxxxxxxxx", k); bb_save_record_result(&full, id, 2, 99999, 7, 999); }
    for (int k = 0; k < BB_SAVE_MAX_ACTS; k++) { snprintf(id, sizeof(id), "act_%02d_xxxxxxxxxxxxxxx", k); bb_save_complete_level(&full, id, 9); }
    for (int k = 0; k < BB_MAX_ITEMS; k++) bb_player_add_item(&full.player, 1000 + k);
    for (int k = 0; k < BB_MAX_ACHS; k++) bb_player_add_ach(&full.player, 2000 + k);
    static uint8_t img[BB_SAVE_MAX_BYTES];
    const size_t n = bb_save_serialize(&full, img, sizeof(img));
    CHECK(full.stage_count == BB_SAVE_MAX_STAGES && full.act_count == BB_SAVE_MAX_ACTS, "both tables filled to the new limits");
    CHECK(n > 0 && n < BB_SAVE_MAX_BYTES && bb_save_deserialize(&back, img, n) && back.stage_count == BB_SAVE_MAX_STAGES &&
          back.act_count == BB_SAVE_MAX_ACTS, "a completely full save fits the 8 KiB image and loads back");
}

/* ---- End-to-end: a fresh save plays its way to Makayla with no unlock-all, mirroring main.c's pass sequence ---- */
typedef struct { const char* id; const char* act; int level_index, level_enum, prereq; } lvl_t;
static const lvl_t k_chain[] = {   /* romfs/stages/index.txt (meta.cfg level_enum / level_prereq) */
    { "cathy_e1", "bb_vol_1", 0, 501, 0 },   { "zoe_e1", "bb_vol_1", 1, 502, 501 }, { "gale_e1", "bb_vol_1", 2, 503, 502 },
    { "claire_e1", "bb_vol_1", 3, 504, 503 }, { "dawna_e1", "bb_vol_1", 4, 505, 504 }, { "zoe_e2", "bb_vol_2", 0, 506, 505 },
    { "cathy_e2", "bb_vol_2", 1, 507, 506 },  { "makayla", "bb_extras", 0, 598, 901 },
};
#define N_CHAIN 8

/* Regression: the index parser stopped at 8 fields, so the 9th (level_prereq) never reached the game and Volume 2 / After Class
 * stayed open on a fresh save. The real rows from romfs/stages/index.txt: */
static void test_index_rows(void) {
    char a[] = "makayla|After Class|makayla|bb_extras|Special Features|2|0|598|901";
    char b[] = "zoe_e2|In The Closet|zoe|bb_vol_2|The Promotion|1|0|506|505";
    char m[] = "not_so_fast_8bae1c|Not so fast|Fanon|fanon_e2e6|Fanon|0|0|0";   /* MOD2DS rows have 8 fields */
    char* f[BB_INDEX_FIELDS];
    CHECK(bb_index_split(a, f, BB_INDEX_FIELDS) == 9 && strcmp(f[0], "makayla") == 0 && strcmp(f[7], "598") == 0 && strcmp(f[8], "901") == 0,
          "After Class: the 9th field (level_prereq 901) is parsed");
    CHECK(bb_index_split(b, f, BB_INDEX_FIELDS) == 9 && strcmp(f[8], "505") == 0, "In The Closet needs Secret Weapon (505)");
    CHECK(bb_index_split(m, f, BB_INDEX_FIELDS) == 8 && strcmp(f[7], "0") == 0, "an 8-field mod row still parses (no prerequisite)");
}

static bool open_lvl(const bb_save_t* s, int k) { return bb_level_open(s, true, k_chain[k].prereq, k_chain[k].act, k_chain[k].level_index); }

/* main.c OUTRO -> RESULTS for a clean pass, then Continue (the power-up is used up there) */
static void play_pass(bb_save_t* s, int k, int diff, int grade, uint32_t hit, uint32_t reg, int powerup, bb_run_outcome_t* out) {
    bb_run_t run;
    memset(&run, 0, sizeof(run));
    run.stage_id = k_chain[k].id; run.level_enum = k_chain[k].level_enum; run.diff = diff; run.grade = grade;
    run.hit = hit; run.registered = reg; run.score = (int32_t)(hit * 100); run.max_combo = hit; run.powerup = powerup; run.clean = true;
    bb_save_record_result(s, run.stage_id, diff, run.score, (uint8_t)grade, (uint16_t)hit);
    bb_save_complete_level(s, k_chain[k].act, (uint8_t)k_chain[k].level_index);
    bb_run_passed(s, &run, out);
    if (powerup) bb_powerup_consume(s, powerup);
}

static void test_fresh_save_to_makayla(void) {
    bb_save_t s, r;
    bb_run_outcome_t out;
    uint8_t img[BB_SAVE_MAX_BYTES];
    memset(&r, 0, sizeof(r));
    bb_save_defaults(&s);
    /* a fresh save: $500, nothing owned, only Virgin Newbie open (Volume 2 and Makayla locked) */
    CHECK(s.settings.unlock_all == 0 && s.player.money == 500 && s.player.exp == 0 && s.player.item_count == 0, "fresh save state");
    CHECK(open_lvl(&s, 0), "fresh: Virgin Newbie is open");
    for (int k = 1; k < N_CHAIN; k++) CHECK(!open_lvl(&s, k), "fresh: every later level is locked (Volume 2 and After Class too)");

    int rank_ups = 0;
    for (int k = 0; k < 7; k++) {   /* Volume 1 and Volume 2 in order, on Normal, an A each */
        CHECK(open_lvl(&s, k), "the level to play is open");
        if (k + 1 < N_CHAIN) CHECK(!open_lvl(&s, k + 1), "the next level stays locked until this clear");
        const int32_t exp0 = s.player.exp;
        play_pass(&s, k, 1, BB_GRADE_A, 90, 100, 0, &out);
        CHECK(s.player.exp >= exp0 + 100, "a Normal clear gives 100 EXP");
        if (out.rank_after > out.rank_before) rank_ups++;
        if (k + 1 < 7) CHECK(open_lvl(&s, k + 1), "the clear opens the next level");
        if (k == 1) {   /* persistence: store and reload mid-way, keep going on the reloaded save */
            size_t n = bb_save_serialize(&s, img, sizeof(img));
            CHECK(n > 0 && bb_save_deserialize(&r, img, n), "save after the second clear and reload");
            CHECK(r.player.exp == s.player.exp && r.player.money == s.player.money && bb_player_has_item(&r.player, 502) &&
                  bb_ach_unlocked(&r, BB_ACH_TASTEOFVICTORY), "the reloaded save keeps EXP, money, the clears and the achievement");
            s = r;
        }
    }
    CHECK(rank_ups >= 3 && bb_rank_from_exp(s.player.exp) >= 4, "seven clears climb several ranks (Professional or higher)");
    CHECK(s.player.stats[BB_STAT_LEVELS_BEATEN] == 7 && bb_ach_unlocked(&s, BB_ACH_TASTEOFVICTORY), "stats + Taste Of Victory");
    for (int k = 0; k < 7; k++) CHECK(bb_player_has_item(&s.player, k_chain[k].level_enum), "each clear owns its level item (Theater / Scene Viewer)");
    CHECK(bb_player_has_item(&s.player, BB_ITEM_COLLECT_DORM_KEY), "In The Dorms gives Cathy's Dorm Key");
    CHECK(!open_lvl(&s, 7), "After Class stays locked until the Dorm Key is read");
    CHECK(bb_collectable_read(&s, BB_ITEM_COLLECT_DORM_KEY) && open_lvl(&s, 7), "reading the Dorm Key in the Collection opens After Class");
    CHECK(!bb_collectable_read(&s, BB_ITEM_COLLECT_DORM_KEY), "reading it again unlocks nothing new");
    CHECK(!bb_collectable_read(&s, 714), "other collectables unlock nothing");
    size_t n = bb_save_serialize(&s, img, sizeof(img));
    CHECK(n > 0 && bb_save_deserialize(&r, img, n) && bb_level_open(&r, true, 901, "bb_extras", 0), "After Class stays open after a reload");

    const int32_t money0 = s.player.money;
    play_pass(&s, 7, 2, BB_GRADE_S, 100, 100, 0, &out);
    CHECK(bb_player_has_item(&s.player, 598) && bb_player_has_item(&s.player, 718), "After Class: its level item and Makayla's collectable");
    CHECK(out.new_ach_count >= 1 && bb_ach_unlocked(&s, BB_ACH_COMBOKING), "a perfect clear earns Combo King");
    play_pass(&s, 7, 2, BB_GRADE_S, 100, 100, 0, &out);
    CHECK(out.new_ach_count == 0 && out.new_item_count == 0 && s.player.money > money0, "a repeat pays again but never repeats achievements or items");

    /* the shop: rank-gated items open with the EXP earned */
    CHECK(bb_shop_buy(&s, BB_ITEM_POWERUP_NO_FAIL, 500, 200) == BB_BUY_OK, "Amateur and up can buy the No Fail Condom");
    /* a No Fail Condom pass records and unlocks like any pass, without the level EXP, and is used up on Continue */
    bb_save_t f;
    bb_save_defaults(&f);
    bb_player_add_item(&f.player, BB_ITEM_POWERUP_NO_FAIL);
    play_pass(&f, 0, 1, BB_GRADE_C, 75, 100, BB_ITEM_POWERUP_NO_FAIL, &out);
    CHECK(open_lvl(&f, 1) && bb_save_find_stage(&f, "cathy_e1") != NULL, "a No Fail pass still opens the next level and records the score");
    CHECK(f.player.exp == 10, "a No Fail pass gives no level EXP (only the achievement's 10)");
    CHECK(!bb_item_owned(&f, BB_ITEM_POWERUP_NO_FAIL), "the condom is used up on Continue");

    /* a mod pass / fail: mod stats and the flat 25 EXP; Give Up costs 10 EXP, never below zero */
    bb_save_t m;
    bb_save_defaults(&m);
    bb_run_t run;
    memset(&run, 0, sizeof(run));
    run.stage_id = "not_so_fast_8bae1c"; run.diff = 2; run.grade = BB_GRADE_B; run.hit = 80; run.registered = 100; run.score = 8000;
    run.clean = true; run.mod = true;
    bb_run_passed(&m, &run, &out);
    CHECK(m.player.stats[BB_STAT_MODS_BEATEN] == 1 && m.player.stats[BB_STAT_LEVELS_BEATEN] == 0 && m.player.exp == 25,
          "a mod pass counts as a mod and gives 25 EXP");
    bb_run_failed(&m, &run, &out);
    CHECK(m.player.stats[BB_STAT_MODS_FAILED] == 1 && m.player.stats[BB_STAT_LEVELS_FAILED] == 0, "a mod fail counts as a mod");
    bb_run_gave_up(&m);
    CHECK(m.player.exp == 15, "Give Up costs 10 EXP");
    bb_run_gave_up(&m); bb_run_gave_up(&m);
    CHECK(m.player.exp == 0, "EXP never goes below zero");

    /* a migrated development save: its unlock-all flag off (a normal build clears it), the progress it earned stays */
    bb_save_t d;
    bb_save_defaults(&d);
    d.settings.unlock_all = 1;
    play_pass(&d, 0, 0, BB_GRADE_B, 85, 100, 0, &out);
    n = bb_save_serialize(&d, img, sizeof(img));
    CHECK(n > 0 && bb_save_deserialize(&r, img, n), "a development save loads");
    r.settings.unlock_all = 0;
    CHECK(open_lvl(&r, 0) && open_lvl(&r, 1) && !open_lvl(&r, 2) && !open_lvl(&r, 7), "without unlock-all it progresses from its own clears");
}

/* Mods hub gauge: 56 mod levels with the 8 built-in stages (MAX_STAGES 64) */
static void test_mod_gauge(void) {
    CHECK(bb_mod_gauge(0, 56) == BB_GAUGE_GOOD && bb_mod_gauge(27, 56) == BB_GAUGE_GOOD, "under half: Good");
    CHECK(bb_mod_gauge(28, 56) == BB_GAUGE_OKAY && bb_mod_gauge(41, 56) == BB_GAUGE_OKAY, "half to three quarters: Okay");
    CHECK(bb_mod_gauge(42, 56) == BB_GAUGE_NEAR_FULL && bb_mod_gauge(55, 56) == BB_GAUGE_NEAR_FULL, "three quarters up to the limit: Near Full");
    CHECK(bb_mod_gauge(56, 56) == BB_GAUGE_FULL && bb_mod_gauge(60, 56) == BB_GAUGE_FULL, "at or over the limit: Full");
    CHECK(bb_mod_gauge(0, 0) == BB_GAUGE_FULL, "no room at all is Full");
    CHECK(strcmp(bb_mod_gauge_name(BB_GAUGE_NEAR_FULL), "Near Full") == 0 && strcmp(bb_mod_gauge_name((bb_mod_gauge_t)9), "Full") == 0, "names");
}

int main(void) {
    printf("=== Beat Banger economy / progression host tests ===\n");
    test_index_rows();
    test_mod_gauge();
    test_fresh_save_to_makayla();
    test_mod_records_never_block_builtin();
    test_ranks_and_rewards();
    test_shop_and_powerups();
    test_runs();
    test_achievement_grid();
    test_save_v2();
    printf("Executed %d assertions: %d failed.\n", g_run, g_failed);
    if (g_failed) { printf("[FAIL]\n"); return 1; }
    printf("[PASS] progress tests succeeded.\n");
    return 0;
}
