/**
 * @file ui_sfx.h
 * @brief Menu / shop / results sounds taken from the original game's own files (romfs:/ui/ui_sfx.bbca, built by
 *        tools/build_ui_assets.py). The enum order is the bank order.
 */
#ifndef BB_UI_SFX_H
#define BB_UI_SFX_H

#include <stdbool.h>

typedef enum {
    BB_UISFX_TOUCH = 0,    /* sfx_menu_touch.wav: UiButton focus (main menu, extras, settings rows/sliders, shop buttons/items/options) */
    BB_UISFX_TICK,         /* ui_tick.ogg: BaseMenu cursor move (level select, difficulty cards, power-up select) */
    BB_UISFX_ACCEPT,       /* ui_accept.ogg */
    BB_UISFX_CLICK,        /* ui_click.ogg: pause toggle/restart, shop Buy/Talk, modifier toggles */
    BB_UISFX_CLICK_ACCEPT, /* ui_click_accept.ogg: a level was chosen */
    BB_UISFX_BACK,         /* ui_back.ogg */
    BB_UISFX_LOCK,         /* ui_lock.ogg */
    BB_UISFX_ERROR,        /* ui_mod_error.ogg: shop item already owned */
    BB_UISFX_BUY,          /* sfx_item_buy.ogg */
    BB_UISFX_DRAWER_OPEN,  /* MainMenu/drawer_open.wav: achievements drawer opens */
    BB_UISFX_DRAWER_CLOSE, /* MainMenu/drawer_close.wav */
    BB_UISFX_VOICE,        /* Jasmine/voice_jasmine.ogg: one speech blip */
    BB_UISFX_OUTFIT_VEST, BB_UISFX_OUTFIT_NOSHIRT, BB_UISFX_OUTFIT_PIERCINGS, BB_UISFX_OUTFIT_TITS,   /* per-outfit equip sounds */
    BB_UISFX_GALLERY_PREVIEW, /* sfx_gallery_preview_image.ogg */
    BB_UISFX_ANALOG_SWITCH,   /* sfx_analog_switch.ogg */
    BB_UISFX_SCORE,        /* results: sfx_score_inc.ogg */
    BB_UISFX_STAR,         /* results: sfx_refill_star.ogg */
    BB_UISFX_CHEER,        /* results: sfx_cheering.wav */
    BB_UISFX_LOW,          /* results: sfx_low_stars.wav */
    BB_UISFX_ACH,          /* ui_ach_unlock.ogg: achievement notification */
    BB_UISFX_UNLOCK,       /* ui_unlock.ogg: item unlock notification */
    BB_UISFX_PAGE_TURN,    /* sfx_gallery_page_turn.ogg: Collection L/R */
    BB_UISFX_ANALOG_CLICK, /* sfx_analog_click.ogg: settings switches / checkboxes */
    BB_UISFX_REVERSE,      /* sfx_reverse.ogg: level passed (ScoreScreen._on_level_passed) */
    BB_UISFX_HIT_STAB,     /* sfx_hit_stab.ogg: results grade star, pitch 1 + 0.1 * star (GradeStar.show_star); main-menu easter-egg taps */
    BB_UISFX_SHUTTER,      /* sfx_shutter.ogg: main-menu easter egg, the shutters close over the loop swap (background_video.gd) */
    BB_UISFX_ARP,          /* menu_arp_3.ogg: same moment */
    BB_UISFX_WHITE_NOISE,  /* env_studio_white_noise.ogg: level failed, looped until Retry / Give Up (bb_uisfx_stop_long) */
    BB_UISFX_GALLERY_ZOOM_IN,  /* sfx_gallery_zoom_in.ogg: gallery lightbox zoom in (gallery.gd zoom) */
    BB_UISFX_GALLERY_ZOOM_OUT, /* sfx_gallery_zoom_out.ogg: zoom out */
    BB_UISFX_HONK,             /* misc_honk.ogg: Jasmine's nose (shop_honk.gd, pitch 1 + honk / 20) */
    BB_UISFX_FAIL_HALLOWEEN    /* level_failed_halloween.wav: GameOver StudioWhiteNoiseSound in the Halloween theme (once, not looped) */
} bb_uisfx_t;

bool bb_uisfx_init(bool ndsp_available);
void bb_uisfx_shutdown(void);
void bb_uisfx_set_volume(float v);          /* 0..1 (master * sfx) */
void bb_uisfx_play(bb_uisfx_t id);
void bb_uisfx_play_pitch(bb_uisfx_t id, float pitch);   /* pitch_scale like AudioStreamPlayer (voice blips 0.98-1.07, equip +-0.2) */
void bb_uisfx_stop_long(void);              /* cut the long results sounds (retry / continue) */

#endif
