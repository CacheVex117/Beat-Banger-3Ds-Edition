/**
 * @file input_map.h
 * @brief Control-scheme lane mapping (pure C99). Lane 0..3 = bottom / right / left / top of the diamond:
 *        face buttons B / A / Y / X, D-pad Down / Right / Left / Up (the D-pad mirrors the face-button positions).
 *
 * The platform layer turns held keys into two 4-bit lane masks (one per input group); this module combines them
 * according to the selected control mode and derives press/release edges from the combined mask. Because edges are
 * taken from the combined mask, a lane pressed through both groups at once produces ONE press (no duplicate
 * judgment), and a hold is released only after both groups let go.
 */
#ifndef BB_INPUT_MAP_H
#define BB_INPUT_MAP_H

#include <stdint.h>

typedef enum {
    BB_CTRL_BUTTONS = 0,  /* B / A / Y / X only */
    BB_CTRL_DPAD    = 1,  /* Down / Right / Left / Up only */
    BB_CTRL_HYBRID  = 2,  /* both groups */
    BB_CTRL_SHOULDERS = 3, /* L / ZL / ZR / R play the Y / X / B / A lanes (shoulder lanes: L -> 2, ZL -> 3, ZR -> 0, R -> 1) */
    BB_CTRL_COUNT   = 4
} bb_control_mode_t;

/* Combined lane mask for the mode (bit i = lane i held); shoulder_lanes is the L / ZL / ZR / R group mapped to lanes. */
static inline uint8_t bb_input_lanes_ex(bb_control_mode_t mode, uint8_t face_lanes, uint8_t dpad_lanes, uint8_t shoulder_lanes) {
    switch (mode) {
        case BB_CTRL_DPAD:      return (uint8_t)(dpad_lanes & 0x0F);
        case BB_CTRL_HYBRID:    return (uint8_t)((face_lanes | dpad_lanes) & 0x0F);
        case BB_CTRL_SHOULDERS: return (uint8_t)(shoulder_lanes & 0x0F);
        case BB_CTRL_BUTTONS:
        default:                return (uint8_t)(face_lanes & 0x0F);
    }
}

static inline uint8_t bb_input_lanes(bb_control_mode_t mode, uint8_t face_lanes, uint8_t dpad_lanes) {
    return bb_input_lanes_ex(mode, face_lanes, dpad_lanes, 0);
}

/* Old 3DS / 2DS have no ZL / ZR (and homebrew cannot read the Circle Pad Pro through hid), so Shoulders mode would leave
 * lanes 0 and 3 unplayable there: it is offered only when the console has the triggers. */
static inline int bb_input_mode_available(bb_control_mode_t mode, int has_zlzr) {
    return (unsigned)mode < (unsigned)BB_CTRL_COUNT && (has_zlzr || mode != BB_CTRL_SHOULDERS);
}

/* The mode a stored setting plays as on this console (a save made on a New 3DS keeps Shoulders; an Old 3DS uses Buttons). */
static inline bb_control_mode_t bb_input_mode_effective(bb_control_mode_t mode, int has_zlzr) {
    return bb_input_mode_available(mode, has_zlzr) ? mode : BB_CTRL_BUTTONS;
}

/* Settings cycling: the next mode in direction dir (+1 / -1) that this console can play. */
static inline bb_control_mode_t bb_input_mode_step(bb_control_mode_t mode, int dir, int has_zlzr) {
    int m = (int)bb_input_mode_effective(mode, has_zlzr);
    for (int i = 0; i < (int)BB_CTRL_COUNT; i++) {
        m = (m + (int)BB_CTRL_COUNT + (dir < 0 ? -1 : 1)) % (int)BB_CTRL_COUNT;
        if (bb_input_mode_available((bb_control_mode_t)m, has_zlzr)) break;
    }
    return (bb_control_mode_t)m;
}

/* Edges between two combined masks. */
static inline void bb_input_edges(uint8_t prev, uint8_t cur, uint8_t* pressed, uint8_t* released) {
    *pressed = (uint8_t)(cur & (uint8_t)~prev);
    *released = (uint8_t)(prev & (uint8_t)~cur);
}

#endif /* BB_INPUT_MAP_H */
