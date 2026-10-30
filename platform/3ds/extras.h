/**
 * @file extras.h
 * @brief Shop / collection / lore / gallery / dialogue data (romfs:/extras, built by tools/build_extras.py).
 */
#ifndef BB_EXTRAS_H
#define BB_EXTRAS_H

#include <stdbool.h>
#include <stdint.h>
#include <citro2d.h>

#define BB_MAX_SHOP     48
#define BB_MAX_COLLECT  32
#define BB_MAX_LORE     8
#define BB_MAX_GALLERY  40
#define BB_MAX_DLG_LINES 96
#define BB_MAX_TALK     32

typedef struct { char cat; int item; char name[40]; int price, req_exp; char desc[100]; } bb_shop_item_t;
typedef struct { int item; char name[40], desc[110], unlock[90]; int icon; } bb_collect_t;
typedef struct { char name[16], caption[48], occupation[40], height[12], lore[520], residence[170], hobbies[200], t3x[32]; } bb_lore_t;
typedef struct { int item; char title[40], sub[80], t3x[24], unlock[90]; int price; } bb_gallery_t;
typedef struct { char kind[24]; int key; char text[110]; } bb_dlg_line_t;          /* shop speech-bubble lines (pool) */
typedef struct { char set[32], input[64], next[32], responses[240]; } bb_talk_t;   /* talk tree option */

typedef struct {
    bool loaded;
    int shop_count, collect_count, lore_count, gallery_count, dlg_count, talk_count;
    bb_shop_item_t shop[BB_MAX_SHOP];
    bb_collect_t   collect[BB_MAX_COLLECT];
    bb_lore_t      lore[BB_MAX_LORE];
    bb_gallery_t   gallery[BB_MAX_GALLERY];
    bb_dlg_line_t  dlg[BB_MAX_DLG_LINES];
    bb_talk_t      talk[BB_MAX_TALK];
} bb_extras_t;

bool bb_extras_load(bb_extras_t* e);                       /* romfs:/extras/ text files */
/* Random pool line of a kind ("entering", "idle", "purchase_success", ...) for outfit key 0; "" if none. */
const char* bb_extras_pool_line(const bb_extras_t* e, const char* kind, uint32_t* rng);

/* Textures: load a t3x from romfs:/extras (NULL on failure); free with C2D_SpriteSheetFree. */
C2D_SpriteSheet bb_extras_load_sheet(const char* name);

#endif
