/**
 * @file save_data.c
 * @brief Settings + progression serialization (see save_data.h).
 */
#include "save_data.h"
#include <string.h>

static const float k_scroll[BB_SCROLL_STEPS] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };

float bb_scroll_multiplier(uint8_t idx) {
    return k_scroll[idx < BB_SCROLL_STEPS ? idx : 0];
}

uint32_t bb_crc32(const uint8_t* data, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

void bb_save_defaults(bb_save_t* s) {
    memset(s, 0, sizeof(*s));
    s->settings.control_mode = BB_CTRL_BUTTONS; /* the verified Milestone 1 behaviour */
    s->settings.calibration_ms = 0;
    s->settings.scroll_idx = 0;
    s->settings.music_vol = 10;
    s->settings.sfx_vol = 10;
    s->settings.unlock_all = 0;   /* a fresh save progresses naturally (unlock-all is a dev-build switch) */
    s->settings.master_vol = 10;
    s->settings.voice_vol = 10;
    s->player.money = 500;   /* SaveManager.gd defaults: player {money: 500, exp: 0} */
}

/* ---- little-endian writers/readers over a bounded buffer ---- */
typedef struct { uint8_t* p; size_t cap, n; bool ok; } wr_t;
static void w8(wr_t* w, uint8_t v) { if (w->n < w->cap) w->p[w->n++] = v; else w->ok = false; }
static void w16(wr_t* w, uint16_t v) { w8(w, (uint8_t)v); w8(w, (uint8_t)(v >> 8)); }
static void w32(wr_t* w, uint32_t v) { w16(w, (uint16_t)v); w16(w, (uint16_t)(v >> 16)); }
static void wid(wr_t* w, const char* id) {
    for (int i = 0; i < BB_SAVE_ID_LEN; i++) w8(w, (uint8_t)id[i]);
}

typedef struct { const uint8_t* p; size_t n, i; bool ok; } rd_t;
static uint8_t r8(rd_t* r) { if (r->i < r->n) return r->p[r->i++]; r->ok = false; return 0; }
static uint16_t r16(rd_t* r) { uint16_t a = r8(r); return (uint16_t)(a | ((uint16_t)r8(r) << 8)); }
static uint32_t r32(rd_t* r) { uint32_t a = r16(r); return a | ((uint32_t)r16(r) << 16); }
static void rid(rd_t* r, char* id) {
    for (int i = 0; i < BB_SAVE_ID_LEN; i++) id[i] = (char)r8(r);
    id[BB_SAVE_ID_LEN - 1] = '\0';
}

size_t bb_save_serialize(const bb_save_t* s, uint8_t* out, size_t cap) {
    if (!s || !out || cap < 16) {
        return 0;
    }
    wr_t w = { out, cap, 16, true }; /* header filled in last */
    w8(&w, s->settings.control_mode);
    w16(&w, (uint16_t)s->settings.calibration_ms);
    w8(&w, s->settings.scroll_idx);
    w8(&w, s->settings.music_vol);
    w8(&w, s->settings.sfx_vol);
    w8(&w, s->settings.unlock_all);
    w8(&w, s->settings.master_vol);
    w8(&w, s->settings.voice_vol);
    w8(&w, s->settings.skip_cutscene);
    w8(&w, s->settings.reduce_motion);
    w8(&w, s->settings.scroll_mode);
    w8(&w, s->settings.theme);
    w32(&w, s->stage_count);
    for (uint32_t i = 0; i < s->stage_count && i < BB_SAVE_MAX_STAGES; i++) {
        const bb_stage_save_t* st = &s->stages[i];
        wid(&w, st->id);
        for (int d = 0; d < 3; d++) { w32(&w, (uint32_t)st->best_score[d]); w8(&w, st->best_grade[d]); w16(&w, st->best_combo[d]); }
    }
    w32(&w, s->act_count);
    for (uint32_t i = 0; i < s->act_count && i < BB_SAVE_MAX_ACTS; i++) {
        wid(&w, s->acts[i].id);
        w8(&w, s->acts[i].levels_unlocked);
    }
    /* v2 player block */
    w32(&w, (uint32_t)s->player.money);
    w32(&w, (uint32_t)s->player.exp);
    w16(&w, s->player.equipped_powerup);
    w16(&w, s->player.menu_music);
    w16(&w, s->player.outfit);
    w16(&w, s->player.item_count);
    for (uint32_t i = 0; i < s->player.item_count && i < BB_MAX_ITEMS; i++) w16(&w, s->player.items[i]);
    w16(&w, s->player.ach_count);
    for (uint32_t i = 0; i < s->player.ach_count && i < BB_MAX_ACHS; i++) w16(&w, s->player.achs[i]);
    for (int i = 0; i < BB_STAT_COUNT; i++) w32(&w, (uint32_t)s->player.stats[i]);
    if (!w.ok) {
        return 0;
    }
    size_t payload = w.n - 16;
    uint32_t crc = bb_crc32(out + 16, payload);
    wr_t h = { out, 16, 0, true };
    w32(&h, BB_SAVE_MAGIC);
    w32(&h, BB_SAVE_VERSION);
    w32(&h, (uint32_t)payload);
    w32(&h, crc);
    return w.n;
}

bool bb_save_deserialize(bb_save_t* s, const uint8_t* data, size_t size) {
    bb_save_defaults(s);
    if (!data || size < 16 || size > BB_SAVE_MAX_BYTES) {
        return false;
    }
    rd_t h = { data, 16, 0, true };
    uint32_t magic = r32(&h), ver = r32(&h), payload = r32(&h), crc = r32(&h);
    if (magic != BB_SAVE_MAGIC || (ver != BB_SAVE_VERSION && ver != 6u && ver != 5u && ver != 4u && ver != 3u && ver != 2u && ver != 1u) || (size_t)payload != size - 16 || bb_crc32(data + 16, payload) != crc) {
        return false;
    }
    bb_save_t t;
    bb_save_defaults(&t);
    rd_t r = { data + 16, payload, 0, true };
    uint8_t cm = r8(&r);
    int16_t cal = (int16_t)r16(&r);
    uint8_t sc = r8(&r), mv = r8(&r), sv = r8(&r), ua = r8(&r);
    if (cm >= BB_CTRL_COUNT || cal < -BB_CAL_LIMIT_MS || cal > BB_CAL_LIMIT_MS || sc >= BB_SCROLL_STEPS || mv > 10 || sv > 10 || ua > 1) {
        return false;
    }
    t.settings.control_mode = cm;
    t.settings.calibration_ms = cal;
    t.settings.scroll_idx = sc;
    t.settings.music_vol = mv;
    t.settings.sfx_vol = sv;
    t.settings.unlock_all = ua;
    if (ver >= 3u) {
        uint8_t mst = r8(&r), vc = r8(&r);
        if (mst > 10 || vc > 10) {
            return false;
        }
        t.settings.master_vol = mst;
        t.settings.voice_vol = vc;
    }
    if (ver >= 4u) {
        uint8_t sk = r8(&r);
        if (sk > 1) {
            return false;
        }
        t.settings.skip_cutscene = sk;
    }
    if (ver >= 5u) {
        uint8_t rm = r8(&r);
        if (rm > 1) {
            return false;
        }
        t.settings.reduce_motion = rm;
    }
    if (ver >= 6u) {
        uint8_t sm = r8(&r);
        if (sm >= BB_SCROLL_MODE_COUNT) {
            return false;
        }
        t.settings.scroll_mode = sm;
    }
    if (ver >= 7u) {
        uint8_t th = r8(&r);
        if (th >= BB_THEME_COUNT) {
            return false;
        }
        t.settings.theme = th;
    }
    uint32_t ns = r32(&r);
    if (ns > BB_SAVE_MAX_STAGES) {
        return false;
    }
    t.stage_count = ns;
    for (uint32_t i = 0; i < ns && r.ok; i++) {
        bb_stage_save_t* st = &t.stages[i];
        rid(&r, st->id);
        for (int d = 0; d < 3; d++) {
            st->best_score[d] = (int32_t)r32(&r);
            st->best_grade[d] = r8(&r);
            st->best_combo[d] = r16(&r);
            if (st->best_grade[d] > 7) {
                return false;
            }
        }
    }
    uint32_t na = r32(&r);
    if (na > BB_SAVE_MAX_ACTS) {
        return false;
    }
    t.act_count = na;
    for (uint32_t i = 0; i < na && r.ok; i++) {
        rid(&r, t.acts[i].id);
        t.acts[i].levels_unlocked = r8(&r);
    }
    if (ver >= 2u) {
        t.player.money = (int32_t)r32(&r);
        t.player.exp = (int32_t)r32(&r);
        t.player.equipped_powerup = r16(&r);
        t.player.menu_music = r16(&r);
        t.player.outfit = r16(&r);
        uint16_t ni = r16(&r);
        if (ni > BB_MAX_ITEMS || t.player.money < 0 || t.player.money > 99999 || t.player.exp < 0 || t.player.exp > 9999) {
            return false;
        }
        t.player.item_count = ni;
        for (uint16_t i = 0; i < ni && r.ok; i++) t.player.items[i] = r16(&r);
        uint16_t na2 = r16(&r);
        if (na2 > BB_MAX_ACHS) {
            return false;
        }
        t.player.ach_count = na2;
        for (uint16_t i = 0; i < na2 && r.ok; i++) t.player.achs[i] = r16(&r);
        for (int i = 0; i < BB_STAT_COUNT; i++) t.player.stats[i] = (int32_t)r32(&r);
    }
    if (!r.ok || r.i != r.n) {
        return false;
    }
    *s = t;
    return true;
}

const bb_stage_save_t* bb_save_find_stage(const bb_save_t* s, const char* id) {
    for (uint32_t i = 0; i < s->stage_count; i++) {
        if (strncmp(s->stages[i].id, id, BB_SAVE_ID_LEN - 1) == 0) {
            return &s->stages[i];
        }
    }
    return NULL;
}

bool bb_save_record_result(bb_save_t* s, const char* stage_id, int diff, int32_t score, uint8_t grade, uint16_t combo) {
    if (diff < 0 || diff > 2) {
        return false;
    }
    bb_stage_save_t* st = (bb_stage_save_t*)bb_save_find_stage(s, stage_id);
    if (!st) {
        if (s->stage_count >= BB_SAVE_MAX_STAGES) {
            return false;
        }
        st = &s->stages[s->stage_count++];
        memset(st, 0, sizeof(*st));
        strncpy(st->id, stage_id, BB_SAVE_ID_LEN - 1);
    }
    bool changed = false;
    bool first = st->best_grade[diff] == 0; /* completed runs always have grade >= F */
    if (first || score > st->best_score[diff]) { st->best_score[diff] = score; changed = true; }
    if (grade > st->best_grade[diff]) { st->best_grade[diff] = grade; changed = true; }
    if (combo > st->best_combo[diff]) { st->best_combo[diff] = combo; changed = true; }
    return changed;
}

uint8_t bb_save_levels_unlocked(const bb_save_t* s, const char* act_id) {
    for (uint32_t i = 0; i < s->act_count; i++) {
        if (strncmp(s->acts[i].id, act_id, BB_SAVE_ID_LEN - 1) == 0) {
            return s->acts[i].levels_unlocked;
        }
    }
    return 0;
}

void bb_save_complete_level(bb_save_t* s, const char* act_id, uint8_t level_index) {
    for (uint32_t i = 0; i < s->act_count; i++) {
        if (strncmp(s->acts[i].id, act_id, BB_SAVE_ID_LEN - 1) == 0) {
            if (s->acts[i].levels_unlocked <= level_index) {
                s->acts[i].levels_unlocked = (uint8_t)(level_index + 1);
            }
            return;
        }
    }
    if (s->act_count < BB_SAVE_MAX_ACTS) {
        bb_act_save_t* a = &s->acts[s->act_count++];
        memset(a, 0, sizeof(*a));
        strncpy(a->id, act_id, BB_SAVE_ID_LEN - 1);
        a->levels_unlocked = (uint8_t)(level_index + 1);
    }
}

int bb_index_split(char* line, char** fld, int max) {
    int nf = 0;
    char* p = line;
    while (nf < max) {
        fld[nf++] = p;
        char* bar = strchr(p, '|');
        if (!bar) break;
        *bar = '\0';
        p = bar + 1;
    }
    return nf;
}

bool bb_save_room_for_extra(const bb_save_t* s, const char* const* builtin_stages, int n_stages, const char* const* builtin_acts, int n_acts) {
    uint32_t need_stages = 0, need_acts = 0;
    for (int i = 0; i < n_stages; i++) if (builtin_stages[i] && !bb_save_find_stage(s, builtin_stages[i])) need_stages++;
    for (int i = 0; i < n_acts; i++) {
        bool found = false;
        for (uint32_t k = 0; k < s->act_count && builtin_acts[i]; k++) if (strncmp(s->acts[k].id, builtin_acts[i], BB_SAVE_ID_LEN - 1) == 0) found = true;
        if (builtin_acts[i] && !found) need_acts++;
    }
    return s->stage_count + need_stages + 1 <= BB_SAVE_MAX_STAGES && s->act_count + need_acts + 1 <= BB_SAVE_MAX_ACTS;
}

bool bb_save_level_unlocked(const bb_save_t* s, const char* act_id, uint8_t level_index) {
    return s->settings.unlock_all || level_index <= bb_save_levels_unlocked(s, act_id);
}

bool bb_player_has_item(const bb_player_t* p, int item) {
    for (uint32_t i = 0; i < p->item_count; i++) {
        if (p->items[i] == (uint16_t)item) return true;
    }
    return false;
}

bool bb_player_add_item(bb_player_t* p, int item) {
    if (item <= 0 || item > 0xFFFF || bb_player_has_item(p, item) || p->item_count >= BB_MAX_ITEMS) {
        return false;
    }
    p->items[p->item_count++] = (uint16_t)item;
    return true;
}

bool bb_player_remove_item(bb_player_t* p, int item) {
    for (uint32_t i = 0; i < p->item_count; i++) {
        if (p->items[i] == (uint16_t)item) {
            p->items[i] = p->items[--p->item_count];
            return true;
        }
    }
    return false;
}

bool bb_player_has_ach(const bb_player_t* p, int ach) {
    for (uint32_t i = 0; i < p->ach_count; i++) {
        if (p->achs[i] == (uint16_t)ach) return true;
    }
    return false;
}

bool bb_player_add_ach(bb_player_t* p, int ach) {
    if (ach <= 0 || ach > 0xFFFF || bb_player_has_ach(p, ach) || p->ach_count >= BB_MAX_ACHS) {
        return false;
    }
    p->achs[p->ach_count++] = (uint16_t)ach;
    return true;
}

void bb_player_stat_add(bb_player_t* p, int stat, int32_t amount) {
    if (stat >= 0 && stat < BB_STAT_COUNT) p->stats[stat] += amount;
}

void bb_player_stat_max(bb_player_t* p, int stat, int32_t value) {
    if (stat >= 0 && stat < BB_STAT_COUNT && value > p->stats[stat]) p->stats[stat] = value;
}
