/**
 * @file anim.c
 * @brief Character spritesheet manager (see anim.h).
 */
#include "anim.h"
#include "hw_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd_u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int64_t rd_i64(const uint8_t* p) {
    uint64_t lo = rd_u32(p), hi = rd_u32(p + 4);
    return (int64_t)(lo | (hi << 32));
}
static float rd_f32(const uint8_t* p) {
    uint32_t u = rd_u32(p);
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

bool bb_anim_init(bb_anim_t* a, const char* manifest_path, const char* gfx_dir) {
    if (!a || !manifest_path || !gfx_dir) {
        return false;
    }
    memset(a, 0, sizeof(*a));
    a->pending_idx = -1;
    strncpy(a->gfx_dir, gfx_dir, sizeof(a->gfx_dir) - 1);

    FILE* fp = fopen(manifest_path, "rb");
    if (!fp) {
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len < 12 || len > 65536) {
        fclose(fp);
        return false;
    }
    uint8_t* buf = (uint8_t*)malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        free(buf);
        fclose(fp);
        return false;
    }
    fclose(fp);

    bool ok = false;
    const uint8_t* p = buf;
    const uint8_t* end = buf + len;
    if (memcmp(p, "BBAN", 4) != 0 || rd_u32(p + 4) != 3) {
        goto done;
    }
    uint32_t sheets = rd_u32(p + 8);
    p += 12;
    if (sheets > BB_ANIM_MAX_SHEETS) {
        goto done;
    }
    for (uint32_t s = 0; s < sheets; s++) {
        if (end - p < 44) goto done;
        bb_anim_sheet_t* sh = &a->sheet[s];
        memcpy(sh->name, p, 31);
        sh->name[31] = '\0';
        sh->frame_count = rd_u32(p + 32);
        sh->frame_w = rd_u16(p + 36);
        sh->frame_h = rd_u16(p + 38);
        sh->page_count = rd_u32(p + 40);
        p += 44;
        if (sh->frame_count > BB_ANIM_MAX_FRAMES || sh->page_count == 0 || sh->page_count > BB_ANIM_MAX_PAGES) {
            goto done;
        }
        if ((size_t)(end - p) < (size_t)sh->page_count * 36 + (size_t)sh->frame_count * 6) goto done;
        for (uint32_t i = 0; i < sh->page_count; i++) {
            memcpy(sh->pages[i].t3x, p, 31);
            sh->pages[i].t3x[31] = '\0';
            sh->pages[i].tex_w = rd_u16(p + 32);
            sh->pages[i].tex_h = rd_u16(p + 34);
            p += 36;
        }
        for (uint32_t f = 0; f < sh->frame_count; f++) {
            sh->frame_page[f] = p[0];
            sh->frame_x[f] = rd_u16(p + 2);
            sh->frame_y[f] = rd_u16(p + 4);
            if (sh->frame_page[f] >= sh->page_count) goto done;
            p += 6;
        }
        a->sheet_count++;
    }
    if (end - p < 4) goto done;
    uint32_t cams = rd_u32(p);
    p += 4;
    if (cams > BB_ANIM_MAX_CAMERA || (size_t)(end - p) < (size_t)cams * 28) {
        goto done;
    }
    for (uint32_t i = 0; i < cams; i++) {
        a->camera[i].timestamp_us = rd_i64(p);
        a->camera[i].pos_x = rd_f32(p + 8);
        a->camera[i].pos_y = rd_f32(p + 12);
        a->camera[i].scale_x = rd_f32(p + 16);
        a->camera[i].scale_y = rd_f32(p + 20);
        a->camera[i].duration_s = rd_f32(p + 24);
        p += 28;
    }
    a->camera_count = cams;   /* (the loop above already stepped past the camera table) */
    if (end - p >= 4) {
        uint32_t nv = rd_u32(p);
        p += 4;
        if ((size_t)(end - p) < (size_t)nv * 68) {
            goto done;
        }
        for (uint32_t i = 0; i < nv; i++, p += 68) {
            char nm[32], vn[32];
            memcpy(nm, p, 31);
            nm[31] = '\0';
            memcpy(vn, p + 32, 31);
            vn[31] = '\0';
            int ni = bb_anim_find(a, nm), vi = bb_anim_find(a, vn);
            if (ni < 0 || vi < 0 || a->sheet[ni].var_count >= 2) {
                continue;
            }
            uint32_t wb = rd_u32(p + 64);
            float w;
            memcpy(&w, &wb, sizeof(w));
            bb_anim_sheet_t* sh = &a->sheet[ni];
            sh->var_idx[sh->var_count] = (uint8_t)vi;
            sh->var_weight[sh->var_count] = w;
            sh->var_count++;
            a->sheet[vi].is_variation = true;
        }
    }
    ok = true;

done:
    free(buf);
    return ok;
}

int bb_anim_find(const bb_anim_t* a, const char* name) {
    if (!a || !name) {
        return -1;
    }
    for (uint32_t i = 0; i < a->sheet_count; i++) {
        if (strcmp(a->sheet[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Wire up the frames that live on texture page pg (already loaded into sh->sheets[pg]). */
static void install_page(bb_anim_t* a, bb_anim_sheet_t* sh, uint32_t pg) {
    a->resident_bytes += (uint32_t)((float)sh->pages[pg].tex_w * (float)sh->pages[pg].tex_h); /* ETC1A4 ~1 B/px */
    C2D_Image base = C2D_SpriteSheetGetImage(sh->sheets[pg], 0);
    C3D_TexSetFilter(base.tex, GPU_LINEAR, GPU_LINEAR);
    float tw = (float)sh->pages[pg].tex_w, th = (float)sh->pages[pg].tex_h;
    for (uint32_t f = 0; f < sh->frame_count; f++) {
        if (sh->frame_page[f] != pg) {
            continue;
        }
        float x = (float)sh->frame_x[f], y = (float)sh->frame_y[f];
        Tex3DS_SubTexture* st = &sh->subtex[f];
        st->width = sh->frame_w;
        st->height = sh->frame_h;
        /* half a texel in from every side: linear filtering at the cell edge would otherwise blend in a line of the
         * neighbouring frame on the page (art that fills its cell, e.g. Legacy 600 x 800 frames) */
        st->left = (x + 0.5f) / tw;
        st->right = (x + (float)sh->frame_w - 0.5f) / tw;
        st->top = 1.0f - (y + 0.5f) / th;                /* texture v runs bottom-up */
        st->bottom = 1.0f - (y + (float)sh->frame_h - 0.5f) / th;
        sh->images[f].tex = base.tex;
        sh->images[f].subtex = st;
    }
}

static bool sheet_complete(const bb_anim_sheet_t* sh) {
    for (uint32_t i = 0; i < sh->page_count; i++) {
        if (!sh->sheets[i]) return false;
    }
    return true;
}

/* The page worker: decompress one .t3x into linear memory. Only the job struct is shared; the main thread installs the result. */
typedef struct { char path[128]; C2D_SpriteSheet result; uint32_t ms; volatile bool done; } page_job_t;
static page_job_t g_job;

static void page_worker(void* arg) {
    page_job_t* j = (page_job_t*)arg;
    uint64_t t0 = osGetTime();
    j->result = C2D_SpriteSheetLoad(j->path);
    j->ms = (uint32_t)(osGetTime() - t0);
    __sync_synchronize();
    j->done = true;
}

/* Collect the page in flight (wait = block until the worker is done; otherwise return false while it still runs). */
static bool finish_job(bb_anim_t* a, bool wait) {
    if (a->pending_idx < 0) {
        return true;
    }
    if (!wait && !g_job.done) {
        return false;
    }
    threadJoin(a->load_thread, U64_MAX);
    threadFree(a->load_thread);
    a->load_thread = NULL;
    bb_anim_sheet_t* sh = &a->sheet[a->pending_idx];
    const uint32_t pg = a->pending_page;
    a->pending_idx = -1;
    if (g_job.result) {
        sh->sheets[pg] = g_job.result;
        install_page(a, sh, pg);
        a->last_load_ms = g_job.ms;
        if (!sh->resident && sheet_complete(sh)) {
            sh->resident = true;
            a->loads++;
        }
    }
    return true;
}

void bb_anim_unload(bb_anim_t* a, int idx) {
    if (!a || idx < 0 || (uint32_t)idx >= a->sheet_count) {
        return;
    }
    if (a->pending_idx == idx) {
        finish_job(a, true);
    }
    bb_anim_sheet_t* sh = &a->sheet[idx];
    for (uint32_t i = 0; i < sh->page_count; i++) {
        if (sh->sheets[i]) {
            a->resident_bytes -= (uint32_t)((float)sh->pages[i].tex_w * (float)sh->pages[i].tex_h);
            C2D_SpriteSheetFree(sh->sheets[i]);
            sh->sheets[i] = NULL;
        }
    }
    sh->resident = false;
}

/* Load one texture page now, on this thread. */
static bool load_one_page(bb_anim_t* a, bb_anim_sheet_t* sh, uint32_t pg) {
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", a->gfx_dir, sh->pages[pg].t3x);
    sh->sheets[pg] = C2D_SpriteSheetLoad(path);
    if (!sh->sheets[pg]) {
        return false;
    }
    install_page(a, sh, pg);
    return true;
}

bool bb_anim_load_step(bb_anim_t* a, int idx) {
    if (!a || idx < 0 || (uint32_t)idx >= a->sheet_count) {
        return true;
    }
    if (!finish_job(a, false)) {
        return false;   /* a page is still decompressing */
    }
    bb_anim_sheet_t* sh = &a->sheet[idx];
    if (sh->resident) {
        return true;
    }
    for (uint32_t i = 0; i < sh->page_count; i++) {
        if (sh->sheets[i]) continue;
        snprintf(g_job.path, sizeof(g_job.path), "%s/%s", a->gfx_dir, sh->pages[i].t3x);
        g_job.result = NULL;
        g_job.done = false;
        a->pending_idx = idx;
        a->pending_page = i;
        a->load_thread = bb_hw_worker_thread(page_worker, &g_job, 64 * 1024);   /* core 2 on New models, else core 0 below us */
        if (!a->load_thread) {   /* no thread: load it here (the old behaviour) */
            a->pending_idx = -1;
            uint64_t t0 = osGetTime();
            load_one_page(a, sh, i);
            a->last_load_ms = (uint32_t)(osGetTime() - t0);
        }
        break;
    }
    if (!sh->resident && sheet_complete(sh)) {
        sh->resident = true;
        a->loads++;
    }
    return sh->resident;
}

static bool do_load(bb_anim_t* a, int idx) {
    bb_anim_sheet_t* sh = &a->sheet[idx];
    if (sh->resident) {
        return true;
    }
    uint64_t t0 = osGetTime();
    for (uint32_t i = 0; i < sh->page_count; i++) {
        if (!sh->sheets[i] && !load_one_page(a, sh, i)) {
            return false;
        }
    }
    sh->resident = true;
    a->loads++;
    a->last_load_ms = (uint32_t)(osGetTime() - t0);
    return true;
}

bool bb_anim_load(bb_anim_t* a, int idx) {
    if (!a || idx < 0 || (uint32_t)idx >= a->sheet_count) {
        return false;
    }
    finish_job(a, true);   /* a page in flight lands first (it may belong to this sheet) */
    return do_load(a, idx);
}

bool bb_anim_load_all(bb_anim_t* a, uint32_t max_bytes) {
    if (!a) {
        return false;
    }
    uint32_t total = 0;
    for (uint32_t i = 0; i < a->sheet_count; i++) {
        for (uint32_t p = 0; p < a->sheet[i].page_count; p++) {
            total += (uint32_t)a->sheet[i].pages[p].tex_w * a->sheet[i].pages[p].tex_h; /* ETC1A4 ~1 byte/px */
        }
    }
    if (total > max_bytes) {
        a->keep_all = false;
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0; i < a->sheet_count; i++) {
        ok = bb_anim_load(a, (int)i) && ok;
    }
    a->keep_all = ok;
    return ok;
}

const C2D_Image* bb_anim_frame(const bb_anim_t* a, int idx, uint32_t frame) {
    if (!a || idx < 0 || (uint32_t)idx >= a->sheet_count) {
        return NULL;
    }
    const bb_anim_sheet_t* sh = &a->sheet[idx];
    if (frame >= sh->frame_count || !sh->sheets[sh->frame_page[frame]]) {
        return NULL;
    }
    return &sh->images[frame];
}

void bb_anim_shutdown(bb_anim_t* a) {
    if (!a) {
        return;
    }
    finish_job(a, true);
    for (uint32_t i = 0; i < a->sheet_count; i++) {
        bb_anim_unload(a, (int)i);
    }
}
