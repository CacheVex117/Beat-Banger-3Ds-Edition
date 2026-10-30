/**
 * @file gallery_ui.c
 * @brief Extras > Gallery (see gallery_ui.h).
 */
#include "gallery_ui.h"
#include "progress.h"
#include "menu_ui.h"
#include "game_view.h"
#include "ui_art.h"
#include "ui_motion.h"
#include "ui_sfx.h"
#include "font.h"
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PICS     64
#define THUMB        96.0f
#define THUMB_COLS   10
#define CENSOR_BASE  64
#define LB_K         (400.0f / 1920.0f)   /* the 1920x1080 lightbox on the 400 px top screen */
#define GRID_COLS    4                    /* MenuGrid column_count */
#define PITCH        76.0f                /* margin / row_margin 420 at 1080p, fitted to 4 columns on 320 px */
#define BOX          72.0f                /* GalleryImage Panel 400 */
#define IMG          69.0f                /* its TextureRect 384 */
#define ROW_Y        132.0f               /* center_position.y: the selected row sits mid-screen, under the band */
#define BAND_H       44.0f                /* the black Polygon2D band (0..200 at 1080p) */
#define ZB 0.30f
#define ZG 0.40f
#define ZT 0.55f
#define ZL 0.70f                          /* lightbox controls */

typedef struct { int tab, item; char title[48], unlock[96], big[32]; int cell, w, h; } pic_t;

static struct {
    bool open;
    bb_save_t* save;
    pic_t p[MAX_PICS];
    int n, first[4], count[4];
    int tab, cur[4];
    C2D_SpriteSheet thumbs, ui, vignette, cross, big;
    int big_idx;                 /* picture held in `big` (-1 none) */
    float sel_t;                 /* when the highlighted picture changed (the large picture loads once the cursor rests) */
    float tab_t0, title_t0;
    /* lightbox */
    bool lb;
    float lb_t0;
    float zoom, z_from, z_to, z_t0;
    float px, py, tx, ty;        /* LightBox/Root position and pan_target (1080p units) */
    float pos_from_x, pos_from_y, pos_t0;
    bool pos_tween;
    int drag_x, drag_y;
    bool dragging;
    /* grid: a vertical drag scrolls (free_off px over the selected-row position); a touch without a drag taps on release */
    bool gdown, gdrag;
    int gx0, gy0, gz0;
    float goff0, free_off;
} G = { .big_idx = -1 };

static const char* const k_tab_desc[4] = { "Gallery", "Achievements Gallery", "Fanart Gallery", "Patron Voted Gallery" };   /* EXTRAS_GALLERY, GALLERY_TITLE_* */

static u32 rgba(u8 r, u8 g, u8 b, float a) { return C2D_Color32(r, g, b, (u8)(a < 0.0f ? 0.0f : (a > 1.0f ? 255.0f : a * 255.0f))); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float ease_out_pow(float t, float p) { t = clampf(t, 0.0f, 1.0f); return 1.0f - powf(1.0f - t, p); }   /* Godot transition < 1 */
static float cubic_out(float t) { return ease_out_pow(t, 3.0f); }

static bool owned(int i) {
    const pic_t* q = &G.p[i];
    return q->item == 0 || (G.save && bb_player_has_item(&G.save->player, q->item));   /* BASE_ITEM and fan art are always unlocked */
}

static C2D_SpriteSheet load_extra(const char* name) {
    char path[64];
    snprintf(path, sizeof(path), "romfs:/extras/%s", name);
    C2D_SpriteSheet s = C2D_SpriteSheetLoad(path);
    if (s) C3D_TexSetFilter(C2D_SpriteSheetGetImage(s, 0).tex, GPU_LINEAR, GPU_LINEAR);
    return s;
}

static void load_data(void) {
    G.n = 0;
    memset(G.first, 0, sizeof(G.first));
    memset(G.count, 0, sizeof(G.count));
    FILE* f = fopen("romfs:/extras/gallery_tabs.txt", "r");
    if (!f) return;
    char line[320];
    while (G.n < MAX_PICS && fgets(line, sizeof(line), f)) {
        char* fld[8] = { 0 };
        int nf = 0;
        char* s = line;
        line[strcspn(line, "\r\n")] = 0;
        while (nf < 8) { fld[nf++] = s; char* bar = strchr(s, '|'); if (!bar) break; *bar = 0; s = bar + 1; }
        if (nf < 8) continue;
        pic_t* q = &G.p[G.n];
        q->tab = atoi(fld[0]);
        q->item = atoi(fld[1]);
        snprintf(q->title, sizeof(q->title), "%.47s", fld[2]);
        snprintf(q->unlock, sizeof(q->unlock), "%.95s", fld[3]);
        snprintf(q->big, sizeof(q->big), "%.31s", fld[4]);
        q->cell = atoi(fld[5]);
        q->w = atoi(fld[6]);
        q->h = atoi(fld[7]);
        if (q->tab < 0 || q->tab > 3) continue;
        if (G.count[q->tab] == 0) G.first[q->tab] = G.n;
        G.count[q->tab]++;
        G.n++;
    }
    fclose(f);
}

void bb_gal_counts(const bb_save_t* save, int* own, int* total) {
    bb_save_t* keep = G.save;
    if (G.n == 0) load_data();
    G.save = (bb_save_t*)save;
    int o = 0;
    for (int i = 0; i < G.n; i++) if (owned(i)) o++;
    G.save = keep;
    if (own) *own = o;
    if (total) *total = G.n;
}

static void free_big(void) {
    if (G.big) { C2D_SpriteSheetFree(G.big); G.big = NULL; }
    G.big_idx = -1;
}

void bb_gal_open(bb_save_t* save) {
    bb_gal_close();
    G.save = save;
    load_data();
    G.thumbs = load_extra("gallery_thumbs.t3x");
    G.ui = load_extra("gallery_ui.t3x");
    G.vignette = load_extra("gallery_vignette.t3x");
    G.cross = load_extra("gallery_cross.t3x");
    G.open = true;
    G.tab = 0;
    for (int t = 0; t < 4; t++) G.cur[t] = 0;
    G.lb = false;
    G.zoom = G.z_from = G.z_to = 1.0f;
    G.tab_t0 = G.title_t0 = G.sel_t = bb_ui_time();
}

void bb_gal_close(void) {
    free_big();
    if (G.thumbs) { C2D_SpriteSheetFree(G.thumbs); G.thumbs = NULL; }
    if (G.ui) { C2D_SpriteSheetFree(G.ui); G.ui = NULL; }
    if (G.vignette) { C2D_SpriteSheetFree(G.vignette); G.vignette = NULL; }
    if (G.cross) { C2D_SpriteSheetFree(G.cross); G.cross = NULL; }
    G.open = false;
    G.lb = false;
}

bool bb_gal_lightbox_open(void) { return G.open && G.lb; }

static int sel(void) { return G.count[G.tab] > 0 ? G.first[G.tab] + G.cur[G.tab] : -1; }

/* gallery.gd check_stare_timer: 5 s on the How Original photo (399) in the lightbox earns Eyes Up Here */
static bool g_gal_dirty = false;
static int g_stare_pic = -1;
static float g_stare_t0 = 0.0f;
bool bb_gal_take_dirty(void) { const bool d = g_gal_dirty; g_gal_dirty = false; return d; }
static void stare_tick(float now) {
    const int i = G.lb ? sel() : -1;
    if (i != g_stare_pic) { g_stare_pic = i; g_stare_t0 = now; }
    if (i >= 0 && G.p[i].item == BB_ITEM_GALLERY_JASMINE_TITFUCK && now - g_stare_t0 >= 5.0f && G.save &&
        bb_ach_unlock(G.save, BB_ACH_EYES_UP_HERE)) {
        g_gal_dirty = true;
        bb_uisfx_play(BB_UISFX_ACH);
    }
}

static void ensure_big(int i) {
    if (i < 0 || i == G.big_idx) return;
    free_big();
    G.big = load_extra(G.p[i].big);
    G.big_idx = G.big ? i : -1;
}

/* ---------------------------------------------------------------- lightbox (gallery.gd) */
static void reset_zoom(void) {
    G.zoom = G.z_from = G.z_to = 1.0f;
    G.dragging = false;
    G.tx = 960.0f; G.ty = 540.0f;
    G.px = 960.0f; G.py = 540.0f;
    G.pos_tween = false;
}

static float cur_zoom(void) {
    const float k = cubic_out((bb_ui_time() - G.z_t0) / 0.3f);
    return G.z_from + (G.z_to - G.z_from) * k;
}

static void zoom(float amount) {   /* gallery.gd zoom(): amount > 0 zooms out (L), < 0 in (R) */
    const float before = G.z_to;
    G.z_from = cur_zoom();
    G.z_to = clampf(G.z_to - amount, 0.5f, 4.0f);
    G.z_t0 = bb_ui_time();
    G.zoom = G.z_to;
    if (G.z_to <= 1.1f) {   /* back to the centre with the zoom */
        G.tx = 960.0f; G.ty = 540.0f;
        G.pos_from_x = G.px; G.pos_from_y = G.py; G.pos_t0 = bb_ui_time(); G.pos_tween = true;
    }
    if (G.z_to != before) bb_uisfx_play(amount < 0.0f ? BB_UISFX_GALLERY_ZOOM_IN : BB_UISFX_GALLERY_ZOOM_OUT);
}

static void pan(float mx, float my) {   /* pan_lightbox */
    G.tx = clampf(G.tx + mx, -960.0f * G.zoom, 960.0f * G.zoom);
    G.ty = clampf(G.ty + my, -270.0f * G.zoom, 540.0f * G.zoom);
}

static void open_lightbox(void) {   /* toggle_preview (on): pop_up from below, sfx_gallery_preview_image */
    const int i = sel();
    if (i < 0) return;
    if (!owned(i)) { bb_uisfx_play(BB_UISFX_LOCK); return; }
    reset_zoom();
    G.lb = true;
    G.lb_t0 = bb_ui_time();
    ensure_big(i);
    bb_uisfx_play(BB_UISFX_GALLERY_PREVIEW);
}

static void next_unlocked(int dir) {   /* get_next_item_unlocked: the next unlocked picture of this tab, no wrap */
    const int n = G.count[G.tab];
    for (int c = G.cur[G.tab] + dir; c >= 0 && c < n; c += dir) {
        if (owned(G.first[G.tab] + c)) {
            G.cur[G.tab] = c;
            G.sel_t = G.title_t0 = bb_ui_time();
            G.lb_t0 = bb_ui_time();   /* reload_image: the preview pops up again */
            ensure_big(sel());
            bb_uisfx_play(BB_UISFX_GALLERY_PREVIEW);
            return;
        }
    }
}

int bb_gal_update(u32 kDown, u32 kRep, u32 kHeld, bool tap, bool touch_held, int tx, int ty) {
    if (!G.open) return 1;
    const float now = bb_ui_time();
    static float last_t = 0.0f;
    float dt = now - last_t;
    last_t = now;
    if (dt > 0.1f || dt < 0.0f) dt = 1.0f / 60.0f;
    const int z = tap ? bb_ui_zone_hit(tx, ty) : -1;
    stare_tick(now);

    if (G.lb) {
        if (z == BB_ZONE_BACK) kDown |= KEY_B;
        if (z == BB_ZONE_LEVEL + 0) zoom(0.5f);
        if (z == BB_ZONE_LEVEL + 2) zoom(-0.5f);
        if (z == BB_ZONE_LEVEL + 1 && G.z_to != 1.0f) { reset_zoom(); bb_uisfx_play(BB_UISFX_GALLERY_ZOOM_OUT); }
        if (z == BB_ZONE_PREV) next_unlocked(-1);
        if (z == BB_ZONE_NEXT) next_unlocked(1);
        if (kDown & (KEY_B | KEY_A)) { reset_zoom(); G.lb = false; return 0; }   /* ui_back: reset_zoom + toggle_preview; ui_accept closes too */
        if (kDown & KEY_L) zoom(0.5f);
        if (kDown & KEY_R) zoom(-0.5f);
        if (G.z_to <= 1.0f) {
            if (kDown & (KEY_DLEFT | KEY_CPAD_LEFT)) next_unlocked(-1);
            if (kDown & (KEY_DRIGHT | KEY_CPAD_RIGHT)) next_unlocked(1);
        } else {   /* _process: lightbox_speed 10 -> 1000 px/s */
            const float v = 1000.0f * dt;
            if (kHeld & (KEY_DUP | KEY_CPAD_UP)) pan(0.0f, v);
            if (kHeld & (KEY_DDOWN | KEY_CPAD_DOWN)) pan(0.0f, -v);
            if (kHeld & (KEY_DLEFT | KEY_CPAD_LEFT)) pan(v, 0.0f);
            if (kHeld & (KEY_DRIGHT | KEY_CPAD_RIGHT)) pan(-v, 0.0f);
            /* drag anywhere off the buttons: the picture follows the stylus (bottom px -> 1080p units at the lightbox scale) */
            if (tap && z < 0) { G.dragging = true; G.drag_x = tx; G.drag_y = ty; }
            if (G.dragging && touch_held) {
                pan((float)(tx - G.drag_x) / LB_K, (float)(ty - G.drag_y) / LB_K);
                G.drag_x = tx; G.drag_y = ty;
            }
        }
        if (!touch_held) G.dragging = false;
        /* the root follows pan_target (pan_smoothing 10) once pop_up has finished; the centring tween runs 0.3 s */
        if (G.pos_tween) {
            const float k = cubic_out((now - G.pos_t0) / 0.3f);
            G.px = G.pos_from_x + (960.0f - G.pos_from_x) * k;
            G.py = G.pos_from_y + (540.0f - G.pos_from_y) * k;
            if (k >= 1.0f) G.pos_tween = false;
        } else if (G.z_to > 1.0f && now - G.lb_t0 > 0.5f) {
            const float a = 1.0f - expf(-10.0f * dt);
            G.px += (G.tx - G.px) * a;
            G.py += (G.ty - G.py) * a;
        }
        return 0;
    }

    /* grid */
    int gz = -1;
    if (tap && !touch_held) gz = z;   /* a tap with no hold (smoke injection) acts at once */
    else {
        if (tap) { G.gdown = true; G.gdrag = false; G.gx0 = tx; G.gy0 = ty; G.gz0 = z; G.goff0 = G.free_off; }
        if (G.gdown && touch_held) {
            const int dy = ty - G.gy0;
            if (dy > 6 || dy < -6) G.gdrag = true;
            if (G.gdrag) {
                const int rows = (G.count[G.tab] + GRID_COLS - 1) / GRID_COLS, row = G.count[G.tab] > 0 ? G.cur[G.tab] / GRID_COLS : 0;
                G.free_off = clampf(G.goff0 + (float)dy, (float)(row - (rows > 0 ? rows - 1 : 0)) * PITCH, (float)row * PITCH);
            }
        }
        if (G.gdown && !touch_held) { G.gdown = false; if (!G.gdrag) gz = G.gz0; }
    }
    if (gz == BB_ZONE_BACK) kDown |= KEY_B;
    if (gz >= BB_ZONE_ACT && gz < BB_ZONE_ACT + 4 && gz - BB_ZONE_ACT != G.tab) {
        G.tab = gz - BB_ZONE_ACT;
        G.free_off = 0.0f;
        G.tab_t0 = G.sel_t = G.title_t0 = now;
        bb_uisfx_play(BB_UISFX_ANALOG_SWITCH);
    }
    if (gz >= BB_ZONE_LEVEL && gz < BB_ZONE_LEVEL + MAX_PICS) {   /* GalleryImage: a tap selects, a tap on the selected one opens it */
        const int i = gz - BB_ZONE_LEVEL;
        if (i == sel()) kDown |= KEY_A;
        else if (i >= G.first[G.tab] && i < G.first[G.tab] + G.count[G.tab]) { G.cur[G.tab] = i - G.first[G.tab]; G.sel_t = G.title_t0 = now; bb_uisfx_play(BB_UISFX_TOUCH); }
    }
    if (kDown & KEY_B) { bb_uisfx_play(BB_UISFX_BACK); return 1; }
    if (kDown & (KEY_L | KEY_R)) {   /* left / right bumper: previous / next tab, sfx_analog_switch */
        G.tab = (G.tab + ((kDown & KEY_R) ? 1 : 3)) % 4;
        G.free_off = 0.0f;
        G.tab_t0 = G.sel_t = G.title_t0 = now;
        bb_uisfx_play(BB_UISFX_ANALOG_SWITCH);
    }
    const int n = G.count[G.tab];
    if (n > 0) {
        int c = G.cur[G.tab];
        const int c0 = c;
        if (kRep & (KEY_DLEFT | KEY_CPAD_LEFT)) c = (c + n - 1) % n;
        if (kRep & (KEY_DRIGHT | KEY_CPAD_RIGHT)) c = (c + 1) % n;
        if (kRep & (KEY_DUP | KEY_CPAD_UP)) c = c - GRID_COLS >= 0 ? c - GRID_COLS : c;
        if (kRep & (KEY_DDOWN | KEY_CPAD_DOWN)) c = c + GRID_COLS < n ? c + GRID_COLS : c;
        if (c != c0) { G.cur[G.tab] = c; G.sel_t = G.title_t0 = now; G.free_off = 0.0f; bb_uisfx_play(BB_UISFX_TOUCH); }
        if (kDown & KEY_A) open_lightbox();
    }
    if (now - G.sel_t > 0.2f) ensure_big(sel());   /* the large picture loads once the cursor rests */
    return 0;
}

/* ---------------------------------------------------------------- drawing helpers */
static void sub_img(C2D_SpriteSheet sh, float sx, float sy, float sw, float sh_, C2D_Image* out, Tex3DS_SubTexture* st) {
    *out = C2D_SpriteSheetGetImage(sh, 0);
    const float tw = (float)out->tex->width, th = (float)out->tex->height;
    st->width = (u16)sw; st->height = (u16)sh_;
    st->left = sx / tw; st->right = (sx + sw) / tw;
    st->top = 1.0f - sy / th; st->bottom = 1.0f - (sy + sh_) / th;
    out->subtex = st;
}

static void draw_sub(C2D_SpriteSheet sh, float sx, float sy, float sw, float sh_, float x, float y, float w, float h, u32 tint, float z) {
    if (!sh) return;
    C2D_Image img;
    Tex3DS_SubTexture st;
    sub_img(sh, sx, sy, sw, sh_, &img, &st);
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, tint, 0.0f);
    C2D_SetTintMode(C2D_TintMult);
    C2D_DrawParams dp = { { x, y, w, h }, { 0.0f, 0.0f }, z, 0.0f };
    C2D_DrawImage(img, &dp, &t);
}

static void thumb(int i, bool censor, float cx, float cy, float s, u32 tint, float z) {
    const int c = censor ? CENSOR_BASE + G.p[i].cell : G.p[i].cell;
    draw_sub(G.thumbs, (float)(c % THUMB_COLS) * THUMB, (float)(c / THUMB_COLS) * THUMB, THUMB, THUMB, cx - s * 0.5f, cy - s * 0.5f, s, s, tint, z);
}

static void ui_icon(int k, float cx, float cy, float s, u32 tint, float z) { draw_sub(G.ui, (float)k * 64.0f, 0.0f, 64.0f, 64.0f, cx - s * 0.5f, cy - s * 0.5f, s, s, tint, z); }

static void outlined(int size, float x, float y, u32 col, u32 align, const char* s, float z) {   /* PhotoTitle: outline 10 + shadow */
    if (!bb_font_ready()) return;
    for (int k = 0; k < 4; k++) bb_font_draw(BB_FACE_BOLD, size, x + ((k & 1) ? 1.0f : -1.0f), y + ((k & 2) ? 1.0f : -1.0f), rgba(0, 0, 0, 1.0f), align, s, z - 0.002f);
    bb_font_draw(BB_FACE_BOLD, size, x + 1.5f, y + 2.0f, rgba(0, 0, 0, 0.6f), align, s, z - 0.003f);
    bb_font_draw(BB_FACE_BOLD, size, x, y, col, align, s, z);
}

/* The picture fitted (keep aspect) into the 1920x1080 TextureRect, scaled `zoom`, centred on (cx, cy) of the top screen. */
static void big_picture(int i, float cx, float cy, float zoom_, float boxw, float boxh, float a, float z) {
    const pic_t* q = &G.p[i];
    if (G.big && G.big_idx == i && q->w > 0 && q->h > 0) {
        const float k = fminf(boxw / (float)q->w, boxh / (float)q->h) * zoom_;
        draw_sub(G.big, 0.0f, 0.0f, (float)q->w, (float)q->h, cx - (float)q->w * k * 0.5f, cy - (float)q->h * k * 0.5f, (float)q->w * k, (float)q->h * k,
                 rgba(255, 255, 255, a), z);
    } else {   /* until the large picture is in: the thumbnail */
        thumb(i, false, cx, cy, fminf(boxw, boxh) * zoom_, rgba(255, 255, 255, a), z);
    }
}

static void sawtooth(float y, float z) {   /* SawtoothVisual under the band */
    for (int k = 0; k < 20; k++) {
        const float x = (float)k * 16.0f;
        const u32 c = C2D_Color32(0, 0, 0, 0xFF);
        C2D_DrawTriangle(x, y, c, x + 16.0f, y, c, x + 8.0f, y + 7.0f, c, z);
    }
}

/* ---------------------------------------------------------------- top screen */
void bb_gal_draw_top(void) {
    if (!G.open) return;
    const float now = bb_ui_time();
    const int i = sel();
    if (G.lb && i >= 0) {   /* the lightbox: the picture alone */
        const float e = now - G.lb_t0;
        C2D_DrawRectSolid(0, 0, ZB, 400, 240, rgba(0, 0, 0, 1.0f));
        const float k = ease_out_pow(e / 0.5f, 5.0f);   /* pop_up: Root y 1620 -> 540 over 0.5 s, transition 0.2 */
        const float ry = G.pos_tween || G.z_to > 1.0f || e >= 0.5f ? G.py : 1620.0f + (540.0f - 1620.0f) * k;
        const float cz = cur_zoom();
        big_picture(i, 200.0f + (G.px - 960.0f) * LB_K, 120.0f + (ry - 540.0f) * LB_K, cz, 1920.0f * LB_K, 1080.0f * LB_K, 1.0f, ZG);
        if (G.z_to > 1.1f) {   /* GalleryCrosshair + Vignette */
            if (G.vignette) draw_sub(G.vignette, 0, 0, 512, 256, 0, 0, 400, 240, rgba(255, 255, 255, 1.0f), ZG + 0.05f);
            if (G.cross) {
                C2D_Image ci = C2D_SpriteSheetGetImage(G.cross, 0);
                const float w = (float)ci.subtex->width, h = (float)ci.subtex->height;
                C2D_DrawImageAt(ci, 200.0f - w * 0.5f, 120.0f - h * 0.5f, ZG + 0.06f, NULL, 1.0f, 1.0f);
            }
        }
        return;
    }
    bb_view_draw_checker(0x30);
    if (i < 0) return;
    const float a = bb_ui_ease_out((now - G.sel_t) / 0.2f);
    const pic_t* q = &G.p[i];
    if (owned(i)) {
        big_picture(i, 200.0f, 120.0f, 1.0f, 400.0f, 240.0f, a, ZG);
        C2D_DrawRectangle(0, 204.0f, ZG + 0.05f, 400, 36.0f, rgba(0, 0, 0, 0), rgba(0, 0, 0, 0), rgba(0, 0, 0, 0.7f), rgba(0, 0, 0, 0.7f));
        outlined(17, 200.0f, 214.0f, rgba(0xFF, 0xFF, 0xFF, a), C2D_AlignCenter, q->title, ZT);
    } else {   /* locked: the censored picture under the Blackout, the lock and the unlock condition */
        thumb(i, true, 200.0f, 120.0f, 240.0f, rgba(255, 255, 255, a), ZG);
        C2D_DrawRectSolid(80.0f, 0.0f, ZG + 0.01f, 240.0f, 240.0f, rgba(0, 0, 0, 0.784f));
        ui_icon(4, 200.0f, 96.0f, 64.0f, rgba(255, 255, 255, a), ZG + 0.02f);
        const char* cond = q->unlock[0] ? q->unlock : "Buy it in the shop.";
        outlined(17, 200.0f, 150.0f, rgba(0xFF, 0xFF, 0xFF, a), C2D_AlignCenter, cond, ZT);
    }
}

/* ---------------------------------------------------------------- bottom screen */
void bb_gal_draw_bottom(void) {
    if (!G.open) return;
    const float now = bb_ui_time();
    bb_view_draw_checker(0x30);
    /* the grid (GalleryAppear: slides up 64 px and fades in, 1 s, transition 0.1) */
    const int n = G.count[G.tab], s = sel();
    const float ga = ease_out_pow((now - G.tab_t0) / 1.0f, 10.0f);
    const float slide = 64.0f * (240.0f / 1080.0f) * (1.0f - ga);
    static float row_y = ROW_Y;
    const int row = n > 0 ? G.cur[G.tab] / GRID_COLS : 0;
    bb_ui_smooth(&row_y, ROW_Y - (float)row * PITCH + G.free_off, G.gdrag ? 30.0f : 12.0f);   /* drive_transform: the selected row slides to the centre (plus a drag) */
    for (int k = 0; k < n; k++) {
        const int i = G.first[G.tab] + k;
        const float cx = 160.0f + ((float)(k % GRID_COLS) - 1.5f) * PITCH, cy = row_y + (float)(k / GRID_COLS) * PITCH + slide;
        if (cy < -BOX || cy > 240.0f + BOX) continue;
        const bool on = i == s, own = owned(i);
        const float dim = on ? 1.0f : 0.2f;   /* highlight: modulate 1 or 0.2 */
        const float br = on ? 1.0f + 0.05f * (0.5f + 0.5f * cosf((now - G.title_t0) * 3.14159f / 1.5f)) : 1.0f;   /* "highlight" 1.05 -> 1 -> 1.05 over 3 s */
        const float bx = BOX * br, ix = IMG * br;
        const u8 v = (u8)(255.0f * dim);
        C2D_DrawRectSolid(cx - bx * 0.5f, cy - bx * 0.5f, ZG - 0.01f, bx, bx, rgba((u8)(0x1A * dim), (u8)(0x1A * dim), (u8)(0x1E * dim), ga));
        thumb(i, !own, cx, cy, ix, C2D_Color32(v, v, v, (u8)(255.0f * ga)), ZG);
        if (!own) {
            C2D_DrawRectSolid(cx - ix * 0.5f, cy - ix * 0.5f, ZG + 0.01f, ix, ix, rgba(0, 0, 0, 0.784f * ga));
            ui_icon(4, cx, cy - 2.0f, 18.0f * br, C2D_Color32(v, v, v, (u8)(255.0f * ga)), ZG + 0.02f);
        }
        if (on && own) {   /* PhotoTitle: title_appear 0.9 -> 1.2 -> 1.0 over 0.2 s */
            const float te = now - G.title_t0;
            const float sc = te < 0.1f ? 0.9f + 3.0f * te : (te < 0.2f ? 1.2f - 2.0f * (te - 0.1f) : 1.0f);
            const int tsz = sc > 1.1f ? 14 : 12;
            const float hw = bb_font_ready() ? bb_font_width(BB_FACE_BOLD, tsz, G.p[i].title) * 0.5f + 3.0f : 0.0f;
            const float tcx = cx - hw < 0.0f ? hw : (cx + hw > 320.0f ? 320.0f - hw : cx);   /* kept on screen at the grid's edges */
            outlined(tsz, tcx, cy + bx * 0.36f - 6.0f, rgba(0xFF, 0xFF, 0xFF, ga), C2D_AlignCenter, G.p[i].title, ZT);
        }
        bb_ui_zone_add(BB_ZONE_LEVEL + i, cx - BOX * 0.5f, cy - BOX * 0.5f, BOX, BOX);
    }
    /* the band: back button, description, tabs */
    C2D_DrawRectSolid(0, 0, ZT + 0.02f, 320, BAND_H, C2D_Color32(0, 0, 0, 0xFF));
    sawtooth(BAND_H, ZT + 0.02f);
    bb_art_icon(7, 20.0f, 21.0f, 26.0f, 0xFFFFFFFFu, ZT + 0.04f);
    bb_ui_zone_add(BB_ZONE_BACK, 2.0f, 3.0f, 36.0f, 36.0f);
    if (bb_font_ready()) bb_font_draw(BB_FACE_BOLD, 14, 40.0f, 13.0f, rgba(0xFF, 0xFF, 0xFF, ga), C2D_AlignLeft, k_tab_desc[G.tab], ZT + 0.04f);
    static const int k_icon[4] = { 0, 1, 2, 3 };
    for (int t = 0; t < 4; t++) {   /* GalleryButtons: the active one white, the rest DIM_GRAY, alpha circles between */
        const float cx = 212.0f + (float)t * 30.0f;
        const u8 v = t == G.tab ? 255 : 105;
        ui_icon(k_icon[t], cx, 21.0f, 22.0f, C2D_Color32(v, v, v, 255), ZT + 0.04f);
        if (t < 3) ui_icon(5, cx + 15.0f, 21.0f, 4.0f, rgba(255, 255, 255, 0.243f), ZT + 0.04f);
        bb_ui_zone_add(BB_ZONE_ACT + t, cx - 14.0f, 4.0f, 28.0f, 34.0f);
    }

    if (G.lb) {   /* the lightbox is on the top screen: the grid dims (ColorRect 0.724) and the zoom controls appear */
        bb_ui_zone_clear();   /* only the lightbox controls are live */
        C2D_DrawRectSolid(0, 0, ZL - 0.01f, 320, 240, rgba(0, 0, 0, 0.724f * ease_out_pow((now - G.lb_t0) / 0.5f, 2.0f)));
        static const char* const lab[3] = { "-", "1:1", "+" };
        for (int b = 0; b < 3; b++) {
            const float cx = 100.0f + (float)b * 60.0f, cy = 196.0f;
            C2D_DrawCircleSolid(cx, cy, ZL, 21.0f, rgba(0xF4, 0xF4, 0xF8, 0.92f));
            C2D_DrawCircleSolid(cx, cy, ZL + 0.001f, 19.0f, rgba(0x10, 0x10, 0x14, 0.95f));
            if (bb_font_ready()) bb_font_draw(BB_FACE_BOLD, b == 1 ? 14 : 21, cx, cy - (b == 1 ? 9.0f : 14.0f), rgba(0xFF, 0xFF, 0xFF, 1.0f), C2D_AlignCenter, lab[b], ZL + 0.01f);
            bb_ui_zone_add(BB_ZONE_LEVEL + b, cx - 24.0f, cy - 24.0f, 48.0f, 48.0f);
        }
        const bool flat = G.z_to <= 1.0f;   /* < > step through the unlocked pictures at 1x (get_next_item_unlocked) */
        if (bb_font_ready()) {
            char zb[16];
            snprintf(zb, sizeof(zb), "x%.1f", G.z_to);
            bb_font_draw(BB_FACE_BOLD, 14, 160.0f, 150.0f, rgba(0xFF, 0xC8, 0x3C, 1.0f), C2D_AlignCenter, zb, ZL + 0.01f);
            bb_font_draw(BB_FACE_BOLD, 26, 22.0f, 96.0f, rgba(0xFF, 0xFF, 0xFF, flat ? 0.9f : 0.25f), C2D_AlignCenter, "<", ZL + 0.01f);
            bb_font_draw(BB_FACE_BOLD, 26, 298.0f, 96.0f, rgba(0xFF, 0xFF, 0xFF, flat ? 0.9f : 0.25f), C2D_AlignCenter, ">", ZL + 0.01f);
            bb_font_draw(BB_FACE_BODY, 10, 160.0f, 60.0f, rgba(0xC8, 0xC8, 0xD2, 0.85f), C2D_AlignCenter,
                         flat ? "L / R  zoom     < >  next picture" : "Drag or D-Pad  pan     L / R  zoom", ZL + 0.01f);
        }
        if (flat) {
            bb_ui_zone_add(BB_ZONE_PREV, 0, 70.0f, 48.0f, 80.0f);
            bb_ui_zone_add(BB_ZONE_NEXT, 272.0f, 70.0f, 48.0f, 80.0f);
        }
        bb_art_icon(7, 20.0f, 21.0f, 26.0f, 0xFFFFFFFFu, ZL + 0.02f);
        bb_ui_zone_add(BB_ZONE_BACK, 2.0f, 3.0f, 36.0f, 36.0f);
    }
}
