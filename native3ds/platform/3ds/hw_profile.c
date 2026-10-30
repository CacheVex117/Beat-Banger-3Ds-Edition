/**
 * @file hw_profile.c
 * @brief Console detection and resource budgets (see hw_profile.h).
 */
#include "hw_profile.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libctru (__system_allocateHeaps) keeps the linear heap at 32 MiB once more than 56 MiB is free and gives the rest to
 * malloc. A 64 MiB Old 3DS application region minus ~1.3 MiB of code, data and stacks leaves ~30.7 MiB of heap; the
 * simulation below keeps a 1.5 MiB margin for that. */
#define O3DS_APP_KIB      (64u * 1024u)
#define O3DS_USED_KIB     1536u

#define ANIM_RESIDENT_BYTES   (12u * 1024u * 1024u)   /* linear heap is 32 MiB on both models: same budget */
#define VIDEO_LOOKAHEAD       3u                      /* tuned on the New 2DS XL (Tracer, console telemetry) */

static bb_hw_t g_hw;
static void* volatile g_ballast;   /* volatile: an unused malloc is optimised away */

void bb_hw_init(void) {
    memset(&g_hw, 0, sizeof(g_hw));
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    g_hw.new3ds = n3ds;
    g_hw.app_mem_kib = (uint32_t)(osGetMemRegionSize(MEMREGION_APPLICATION) / 1024);
    g_hw.linear_heap_kib = envGetLinearHeapSize() / 1024;
    g_hw.heap_kib = envGetHeapSize() / 1024;

    /* New models: 804 MHz + L2 and core 2 for the loaders (both hardware-verified on the New 2DS XL).
     * Old models: core 0, one priority below the game thread (they only take time the game thread leaves idle). */
    g_hw.speedup = n3ds;
    g_hw.has_zlzr = n3ds;
    g_hw.worker_core = n3ds ? 2 : 0;
    osSetSpeedupEnable(g_hw.speedup);   /* no effect on old models */

    g_hw.anim_resident_bytes = ANIM_RESIDENT_BYTES;
    g_hw.video_lookahead = VIDEO_LOOKAHEAD;
}

const bb_hw_t* bb_hw(void) {
    return &g_hw;
}

Thread bb_hw_worker_thread(ThreadFunc fn, void* arg, size_t stack_size) {
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    Thread t = threadCreate(fn, arg, stack_size, prio + 1, g_hw.worker_core, false);
    if (!t && g_hw.worker_core != 0) t = threadCreate(fn, arg, stack_size, prio + 1, 0, false);
    return t;
}

uint32_t bb_hw_heap_free_kib(void) {
    struct mallinfo mi = mallinfo();
    uint32_t claimed = (uint32_t)mi.arena, freed = (uint32_t)mi.fordblks;
    uint32_t total = envGetHeapSize();
    return (total > claimed ? total - claimed : 0) / 1024 + freed / 1024;
}

void bb_hw_simulate_old3ds_memory(void) {
    const uint32_t o3ds_heap_kib = O3DS_APP_KIB - g_hw.linear_heap_kib - O3DS_USED_KIB;
    if (g_hw.new3ds || g_ballast || g_hw.linear_heap_kib >= O3DS_APP_KIB || g_hw.heap_kib <= o3ds_heap_kib) {
        return;
    }
    uint32_t kib = g_hw.heap_kib - o3ds_heap_kib;
    uint32_t free_kib = bb_hw_heap_free_kib();
    if (kib > free_kib) kib = free_kib;
    g_ballast = malloc((size_t)kib * 1024u);
    if (g_ballast) g_hw.sim_ballast_kib = kib;
}

void bb_hw_describe(char* out, size_t cap) {
    snprintf(out, cap, "hw console=%s cpu=%s worker_core=%d app_mem=%lu KiB linear_heap=%lu KiB "
             "heap=%lu KiB heap_free=%lu KiB sim_ballast=%lu KiB anim_budget=%lu KiB video_lookahead=%lu",
             g_hw.new3ds ? "new3ds" : "old3ds", g_hw.speedup ? "804MHz+L2" : "268MHz", g_hw.worker_core,
             (unsigned long)g_hw.app_mem_kib, (unsigned long)g_hw.linear_heap_kib, (unsigned long)g_hw.heap_kib,
             (unsigned long)bb_hw_heap_free_kib(), (unsigned long)g_hw.sim_ballast_kib,
             (unsigned long)(g_hw.anim_resident_bytes / 1024), (unsigned long)g_hw.video_lookahead);
}
