/**
 * @file boot_ui.h
 * @brief The boot sequence before the main menu (replaces the old PRESS A title).
 *
 *   0  WARNING / THE FOLLOWING CONTENT IS 18+ (top), adult-content notice + A Continue / B Exit (bottom), both on the checkerboard
 *   1  the fan-port disclaimer (top, checkerboard) and the cast key art on black (bottom); A or a touch continues
 *   2  the Bunfan Studios intro video (top, drawn by main.c with video.c) over the dimmed rabbit logo on black (bottom);
 *      in the Halloween theme the splash_bunny_halloween animation instead (top, drawn here, 13 s; main.c streams its sound)
 *
 * Presentation only; main.c owns the steps, fades, timers and input. Assets from tools/build_boot.py (romfs:/boot/).
 */
#ifndef BB_BOOT_UI_H
#define BB_BOOT_UI_H

#include <3ds.h>
#include <citro2d.h>

#define BB_BOOT_FADE_MS 250   /* each screen fades in from black and out to black */
#define BB_BOOT_HALLOWEEN_MS 13000   /* splash_bunny_halloween "splash" length: splash_finished at 13 s */

void bb_boot_load(void);
void bb_boot_free(void);
/* `black` = 0..1 overlay for the fades; touch zones (bottom): BB_ZONE_LEVEL (continue), BB_ZONE_BACK (exit, step 0). */
void bb_boot_top(int step, float black);
void bb_boot_bottom(int step, float black);

#endif
