/**
 * @file anim.h
 * @brief Character spritesheet manager for the 3DS top screen (Citro2D).
 *
 * Loads the BBAN v2 manifest written by tools/convert_sprites.py: per-sheet frame
 * geometry + texture pages (.t3x) and the stage camera keyframes.  Sheets are
 * addressed by the loop name used in the chart (e.g. "cathy_e1_headlick").
 * Residency is managed by the caller through bb_anim_set_resident(); only the
 * current and the next sheet need to be in memory.
 */
#ifndef BB_ANIM_H
#define BB_ANIM_H

#include <stdint.h>
#include <stdbool.h>
#include <3ds.h>
#include <citro2d.h>

#define BB_ANIM_MAX_SHEETS  64   /* SD-card mods (Arte's Rush: 40); over the resident budget sheets stream */
#define BB_ANIM_MAX_PAGES    4
#define BB_ANIM_MAX_FRAMES  16
#define BB_ANIM_MAX_CAMERA 256   /* camera keyframes per level (SD-card mods: Lola Bunny - Horny Bunny Season has 78) */

typedef struct {
    char     t3x[32];
    uint16_t tex_w, tex_h;
} bb_anim_page_t;

typedef struct {
    char            name[32];
    uint32_t        frame_count;
    uint16_t        frame_w, frame_h;
    uint32_t        page_count;
    bb_anim_page_t  pages[BB_ANIM_MAX_PAGES];
    uint8_t         frame_page[BB_ANIM_MAX_FRAMES];
    uint16_t        frame_x[BB_ANIM_MAX_FRAMES];
    uint16_t        frame_y[BB_ANIM_MAX_FRAMES];

    /* Weighted variation sheets (game_character.try_variation); is_variation sheets are only drawn via their parent */
    uint8_t               var_count;
    uint8_t               var_idx[2];
    float                 var_weight[2];
    bool                  is_variation;

    /* Runtime residency */
    bool                  resident;
    C2D_SpriteSheet       sheets[BB_ANIM_MAX_PAGES];
    Tex3DS_SubTexture     subtex[BB_ANIM_MAX_FRAMES];
    C2D_Image             images[BB_ANIM_MAX_FRAMES];
} bb_anim_sheet_t;

typedef struct {
    int64_t timestamp_us;
    float   pos_x, pos_y, scale_x, scale_y, duration_s;
} bb_anim_camera_key_t;

typedef struct {
    uint32_t              sheet_count;
    bb_anim_sheet_t       sheet[BB_ANIM_MAX_SHEETS];
    uint32_t              camera_count;
    bb_anim_camera_key_t  camera[BB_ANIM_MAX_CAMERA];
    char                  gfx_dir[64];

    bool                  keep_all;         /* every sheet stays resident (set by bb_anim_load_all) */

    /* Background page loader (one outstanding page): the worker decompresses the .t3x, the main thread installs it */
    Thread                load_thread;
    int                   pending_idx;      /* sheet of the page in flight, -1 when idle */
    uint32_t              pending_page;

    /* Diagnostics */
    uint32_t              loads;
    uint32_t              last_load_ms;
    uint32_t              resident_bytes;
} bb_anim_t;

/* Parse a .bbanim manifest; gfx_dir is where the .t3x pages live (e.g. "romfs:/gfx"). */
bool bb_anim_init(bb_anim_t* a, const char* manifest_path, const char* gfx_dir);
void bb_anim_shutdown(bb_anim_t* a);

/* Index of the sheet with this loop name, or -1. */
int  bb_anim_find(const bb_anim_t* a, const char* name);

/* Load every sheet now (no mid-song loads) if the total texture memory fits max_bytes; returns true if done. */
bool bb_anim_load_all(bb_anim_t* a, uint32_t max_bytes);

/* Bring a sheet in without blocking: each call installs a page the worker has finished and starts the next missing one on
 * the worker (bb_hw_worker_thread: core 2 on New 3DS, else a lower-priority thread on core 0). Call every frame; returns true once the sheet is resident.
 * bb_anim_load / bb_anim_unload / bb_anim_shutdown wait for a page in flight first. */
bool bb_anim_load_step(bb_anim_t* a, int sheet_idx);

/* Load/free texture pages. Returns false if loading failed. */
bool bb_anim_load(bb_anim_t* a, int sheet_idx);
void bb_anim_unload(bb_anim_t* a, int sheet_idx);

/* Frame image (NULL if the sheet is not resident or frame out of range). */
const C2D_Image* bb_anim_frame(const bb_anim_t* a, int sheet_idx, uint32_t frame);

#endif /* BB_ANIM_H */
