/**
 * @file collection_ui.h
 * @brief Extras > Collection, rebuilt from scenes/Collection (collection.gd, collection_item.gd, collection_popup.gd).
 *
 * BOTTOM: the scene as in the game -- the dimmed background, the spiral book and its decor strokes (romfs:/extras/coll_scene.t3x),
 *         the items of the current page at their Collection.tscn offsets (locked = black silhouette, unlocked = dim_color, focused =
 *         WHITE_SMOKE with UiButton's 0.5 s expo tween), the page arrows and the back button.
 * TOP:    the standard checkerboard; an opened item pops up there with the original "popup" / "close" animations and the looping
 *         "hover" scale, while the bottom dims under the popup's ColorRect until it is closed.
 * Data: tools/build_collection.py (coll_items.t3x, coll_layout.txt).
 *
 * Input: D-Pad / Circle Pad move the focus (the original focus neighbours; past the outer column turns the page), L / R turn pages,
 * A opens (locked: ui_lock), B closes the popup or leaves. Touch: a tap focuses an item and a tap on the focused one opens it; the
 * arrows turn pages; any tap closes the popup. Touch zones are registered by bb_coll_draw_bottom (BB_ZONE_LEVEL + slot,
 * BB_ZONE_PREV / BB_ZONE_NEXT, BB_ZONE_BACK) and read by the next bb_coll_update.
 */
#ifndef BB_COLLECTION_UI_H
#define BB_COLLECTION_UI_H

#include <stdbool.h>
#include <3ds.h>
#include "extras.h"
#include "save_data.h"

void bb_coll_open(bb_extras_t* ex, bb_save_t* save);   /* loads the art + layout, page 1, first item focused */
void bb_coll_close(void);
/* True once after the Collection changed the save (reading the Dorm Key unlocks After Class): the caller stores it. */
bool bb_coll_take_dirty(void);
bool bb_coll_focus_item(int item);   /* smoke scripts: focus the slot holding `item` */                               /* frees the art */
/* 0 while active, 1 when the player backed out (ui_back already played). */
int  bb_coll_update(u32 kDown, u32 kRep, bool tap, int tx, int ty);
void bb_coll_draw_top(void);
void bb_coll_draw_bottom(void);
bool bb_coll_popup_open(void);                          /* an item is shown on the top screen */

#endif
