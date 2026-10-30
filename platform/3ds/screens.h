/**
 * @file screens.h
 * @brief Shop, Extras (characters, collection, gallery, theater), Achievements and power-up selection screens (both screens).
 *
 * Behaviour follows the reference game: scenes/Shop (Buy/Talk/Outfit with Jasmine and her speech pool, rank-gated
 * items, money), scenes/Lore, scenes/Collection, scenes/Gallery, scenes/Achievements and PowerUpSelectionMenu.
 * Pure presentation + input over bb_save_t / progress.h; the caller persists the save when `dirty` is set.
 */
#ifndef BB_SCREENS_H
#define BB_SCREENS_H

#include <3ds.h>
#include <citro2d.h>
#include "save_data.h"
#include "extras.h"

typedef enum { SC_SHOP = 0, SC_EXTRAS, SC_ACHIEVEMENTS, SC_POWERUP } sc_root_t;

typedef enum {
    SCN_NONE = 0, SCN_SHOP, SCN_SHOP_LIST, SCN_SHOP_TALK, SCN_SHOP_OUTFIT,
    SCN_EXTRAS, SCN_CHARS, SCN_COLLECT, SCN_GALLERY, SCN_GALLERY_VIEW, SCN_ACH, SCN_POWERUP, SCN_THEATER,   /* Boop is a mode of SCN_SHOP (booping) */
    SCN_MUSIC, SCN_SCENEVIEW   /* Extras > Music (music_ui.c, the Sound Test) and Scene Viewer (sceneview_ui.c); main.c routes their input */
} sc_node_t;

#define SC_MAX_ROWS 48
#define SC_MAX_REEL_STAGES 12   /* Theater: two reels (before / after) per stage */

typedef struct {
    bb_extras_t* ex;
    bb_save_t*   save;
    sc_node_t    node;
    int          cursor[16];          /* per-node cursor memory (indexed by node) */
    int          shop_cat;            /* 0 P, 1 G, 2 M, 3 O */
    int          level_enums[16];     /* meta.cfg level_enum of every playable stage (for "beat every level") */
    int          level_count;
    char         talk_set[32];
    char         speech[200];         /* current Jasmine line */
    uint32_t     rng;
    bool         dirty;               /* save changed: caller should persist */
    int          chosen_powerup;      /* SC_POWERUP result */
    int          result;              /* 0 active, 1 closed, 2 power-up chosen */
    int          page;                /* characters page / gallery view index */

    C2D_SpriteSheet jasmine, detail, atlas;   /* loaded art (any may be NULL) */
    Tex3DS_SubTexture st_a, st_b;
    int          detail_key;          /* what `detail` currently holds */
    int          jasmine_outfit;
    int          outfit_page;         /* clothes_menu outfit_menus: 0 "Outfits", 1 "Seasonal" (L / R) */

    char lab[SC_MAX_ROWS][64];
    char val[SC_MAX_ROWS][32];
    u32  col[SC_MAX_ROWS];
    int  icon[SC_MAX_ROWS];          /* shop item id per row for draw_list icons (0 = none) */
    bool use_icons;
    int  icon_cat;
    C2D_SpriteSheet gthumb[BB_MAX_GALLERY];   /* gallery grid thumbnails (loaded while the gallery is open) */
    bool gthumb_loaded;
    float speech_t0;                 /* shop: when the current line started (0.8 s reveal, voice blips, mouth shapes) */
    float idle_t0;                   /* shop: last time Jasmine spoke (idle line after 20 s) */
    float shop_open_t;               /* shop: slide-in start (once per visit) */
    float react_t0;                  /* shop: start of the current reaction */
    float list_t0;                   /* shop: row slide-in start (buy list / category change) */
    int   react_kind, speech_blips, mouth_n, mouth_shape, mouth_last;
    bool  booping;                   /* shop: the Boop page (Jasmine up close on the bottom screen, the empty room dimmed on top) */
    int   honk_amount;               /* shop_honk.gd: honks this visit */
    float opt_w[4];                  /* shop options: ShopOptionsButton width (480 -> 582 when focused, expo 0.5 s) */
    int   cash_from, cash_to;        /* PlayerCash.update: the amount counts up over 1 s (expo out) */
    float cash_t0;
    char note[96];                   /* extras: "not available" line */

    /* Extras > Theater (theater.gd): the pre / post cutscene of every playable stage, registered once by main.c */
    int     reel_stages;
    char    reel_id[SC_MAX_REEL_STAGES][32], reel_char[SC_MAX_REEL_STAGES][24], reel_title[SC_MAX_REEL_STAGES][48], reel_act[SC_MAX_REEL_STAGES][24];
    int     reel_enum[SC_MAX_REEL_STAGES];   /* the level's clear item (meta.cfg level_enum): its pre and post reels open with it */
    int     theater_pick;            /* result 3: reel index (stage = pick / 2, after-scene = pick & 1) */
} bb_screens_t;

void bb_screens_init(bb_screens_t* sc, bb_extras_t* ex, bb_save_t* save);
void bb_screens_open(bb_screens_t* sc, sc_root_t root);
/* A stage whose cutscenes the Theater can replay (stage id, character, level title, act id, the level's clear item). */
void bb_screens_add_reel_stage(bb_screens_t* sc, const char* id, const char* character, const char* title, const char* act, int level_enum);
/* Theater reel `pick` is unlocked once its level has been cleared (both its before- and after-scene), as in the original. */
bool bb_screens_reel_open(const bb_screens_t* sc, int pick);
/* Returns 0 while active, 1 when the root screen was left (back), 2 when a power-up was chosen (sc->chosen_powerup),
 * 3 when a Theater reel was chosen (sc->theater_pick; the caller plays the cutscene and reopens the Theater). */
int  bb_screens_update(bb_screens_t* sc, u32 kDown, u32 kRep);
/* Bottom-screen touch (zones drawn last frame): an item selects, a tap on the selected item presses A; back = B, arrows = L / R. */
void bb_screens_touch(bb_screens_t* sc, bool tap, int tx, int ty, u32* kDown);
void bb_screens_draw(bb_screens_t* sc, C2D_TextBuf tb);
/* Every screen is dual-screen: the bottom holds the menu and its touch targets (bb_screens_draw_bottom), the top the picture /
 * text / logo that goes with it (bb_screens_draw). Jasmine's Shop: the room, Jasmine and her dialogue on top. */
bool bb_screens_is_shop(const bb_screens_t* sc);
void bb_screens_draw_bottom(bb_screens_t* sc, C2D_TextBuf tb);
/* Power-up selection on the BOTTOM screen (PowerUpSelectionMenu): touch zones BB_ZONE_LEVEL + i per icon and BB_ZONE_BACK. */
void bb_screens_draw_powerup_bottom(bb_screens_t* sc, C2D_TextBuf tb);
void bb_screens_close(bb_screens_t* sc);

#endif
