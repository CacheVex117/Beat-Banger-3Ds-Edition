/**
 * @file gallery_ui.h
 * @brief Extras > Gallery, rebuilt from scenes/Gallery (gallery.gd, galleries.gd, GalleryImage, LightBox).
 *
 * BOTTOM: the black band with the back button, the tab's description (Gallery / Achievements Gallery / Fanart Gallery / Patron Voted
 * Gallery) and the four tab icons (L / R or touch), and the 4-column MenuGrid (unselected pictures dim to 0.2, the selected one
 * breathes 1.05 <-> 1.0 and shows its title; locked = censored picture, Blackout 0.784 and the lock). While the lightbox is open the
 * bottom dims and carries the zoom controls (-, 1:1, +, < >) and a drag area that pans the picture.
 * TOP: the highlighted picture, large; in the lightbox the picture alone, sliding up ("pop_up"), with the original zoom (L / R in 0.5
 * steps, 0.5..4.0, 0.3 s cubic tween), D-pad / circle pad / drag panning, and the crosshair + vignette above 1.1x.
 * Assets: tools/build_gallery.py (romfs:/extras/gallery_*). Touch zones: BB_ZONE_LEVEL + picture, BB_ZONE_ACT + tab, BB_ZONE_PREV /
 * BB_ZONE_NEXT, BB_ZONE_BACK.
 */
#ifndef BB_GALLERY_UI_H
#define BB_GALLERY_UI_H

#include <3ds.h>
#include <stdbool.h>
#include "save_data.h"

void bb_gal_open(bb_save_t* save);
void bb_gal_close(void);
/* True once after the Gallery changed the save (Eyes Up Here): the caller stores it. */
bool bb_gal_take_dirty(void);
/* 0 active, 1 back (ui_back already played). kHeld drives the D-pad / circle pad pan; touch_held + tx/ty the drag pan. */
int  bb_gal_update(u32 kDown, u32 kRep, u32 kHeld, bool tap, bool touch_held, int tx, int ty);
void bb_gal_draw_top(void);
void bb_gal_draw_bottom(void);
bool bb_gal_lightbox_open(void);
/* Pictures in the four tabs and how many of them the save owns (for the Extras "n/m" count). */
void bb_gal_counts(const bb_save_t* save, int* owned, int* total);

#endif
