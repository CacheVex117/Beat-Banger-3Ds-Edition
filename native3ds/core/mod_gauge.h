/**
 * @file mod_gauge.h
 * @brief How full the SD card's mod level list is (Mods hub gauge, pure C99).
 *
 * The game lists at most MAX_STAGES levels, shared with the built-in ones (56 mod levels with 8 built-in). Good below half,
 * Okay below three quarters, Near Full until the limit, Full at or over it (levels past it are not listed).
 */
#ifndef BB_MOD_GAUGE_H
#define BB_MOD_GAUGE_H

typedef enum {
    BB_GAUGE_GOOD = 0,
    BB_GAUGE_OKAY,
    BB_GAUGE_NEAR_FULL,
    BB_GAUGE_FULL
} bb_mod_gauge_t;

static inline bb_mod_gauge_t bb_mod_gauge(int used, int cap) {
    if (cap <= 0 || used >= cap) return BB_GAUGE_FULL;
    if (used * 4 < cap * 2) return BB_GAUGE_GOOD;
    if (used * 4 < cap * 3) return BB_GAUGE_OKAY;
    return BB_GAUGE_NEAR_FULL;
}

static inline const char* bb_mod_gauge_name(bb_mod_gauge_t g) {
    static const char* const names[4] = { "Good", "Okay", "Near Full", "Full" };
    return names[(unsigned)g < 4u ? (unsigned)g : 3u];
}

#endif /* BB_MOD_GAUGE_H */
