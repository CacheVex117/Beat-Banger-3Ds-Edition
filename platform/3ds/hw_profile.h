/**
 * @file hw_profile.h
 * @brief The one place that knows which console the game runs on and what it can afford.
 *
 * Detected once at the top of main() (bb_hw_init, before anything is loaded). Systems ask this profile for cores and
 * budgets instead of calling APT_CheckNew3DS or hard-coding them, so the New 3DS / New 2DS XL path stays exactly as
 * tuned on hardware and every Old 3DS / 2DS difference lives in hw_profile.c.
 *
 * What differs (measured / derived, see SPRINT_HANDOFF.md "Old 3DS support"):
 *  - CPU: New models run at 804 MHz with the L2 cache (osSetSpeedupEnable); old models at 268 MHz without it.
 *  - Cores: New models give applications core 2, used for the background page loaders; old models have no core 2,
 *    so the loaders run on core 0 below the game thread (they only take time the game thread leaves idle).
 *  - Memory: libctru keeps the linear heap at 32 MiB on both (textures, audio), so every linear budget is shared.
 *    Only the malloc heap is smaller (~30 MiB instead of ~90 MiB); the game uses a few MiB of it.
 *  - Buttons: old models have no ZL / ZR (the Shoulders control scheme needs them).
 */
#ifndef BB_HW_PROFILE_H
#define BB_HW_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <3ds.h>

typedef struct {
    bool     new3ds;            /* New 3DS / New 3DS XL / New 2DS XL (APT_CheckNew3DS) */
    bool     speedup;           /* 804 MHz + L2 cache requested */
    bool     has_zlzr;          /* ZL / ZR exist (New models only; Shoulders controls need them) */
    int      worker_core;       /* preferred core for the background loaders (anim.c pages, video.c pages) */
    uint32_t app_mem_kib;       /* APPLICATION memory region as reported by the kernel */
    uint32_t linear_heap_kib;   /* libctru's linear heap (textures, audio buffers) */
    uint32_t heap_kib;          /* libctru's malloc heap */
    uint32_t sim_ballast_kib;   /* heap held back so an emulated Old 3DS has a real one's heap */

    /* budgets (the same numbers on both models unless a measurement says otherwise) */
    uint32_t anim_resident_bytes;   /* load every character sheet up front when they fit in this; above it they stream */
    uint32_t video_lookahead;       /* video pages kept loaded ahead of the playhead */
} bb_hw_t;

/* Detect the console and set the CPU mode. Call first thing in main(). */
void            bb_hw_init(void);
const bb_hw_t*  bb_hw(void);

/* A background loader thread one priority below the caller, on the profile's worker core (falls back to core 0).
 * NULL if no thread could be created (the caller then loads synchronously, as before). */
Thread          bb_hw_worker_thread(ThreadFunc fn, void* arg, size_t stack_size);

/* malloc heap still available (never claimed + freed), for telemetry. */
uint32_t        bb_hw_heap_free_kib(void);

/* Emulator smoke runs on an emulated Old 3DS: hold back the heap above what a real Old 3DS gives this app, so memory
 * is tested against the real console's numbers. Azahar hands a .3dsx 96-124 MiB even in Old-3DS mode. */
void            bb_hw_simulate_old3ds_memory(void);

/* One line for telemetry.log describing the profile in effect. */
void            bb_hw_describe(char* out, size_t cap);

#endif /* BB_HW_PROFILE_H */
