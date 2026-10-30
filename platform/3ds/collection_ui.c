/**
 * @file collection_ui.c
 * @brief Extras > Collection (scenes/Collection), see collection_ui.h.
 */
#include "collection_ui.h"
#include "progress.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <citro2d.h>

#include "font.h"
#include "game_view.h"
#include "menu_ui.h"
#include "ui_art.h"
#include "ui_motion.h"
#include "ui_sfx.h"

/* collection_popup.tscn plays ui_scrap_book.wav with "popup"; the UI bank has no such clip unless it defines this. */
#ifdef BB_UISFX_HAS_SCRAP_BOOK
#define COLL_POPUP_SFX BB_UISFX_SCRAP_BOOK
#else
#define COLL_POPUP_SFX BB_UISFX_GALLERY_PREVIEW
#endif

#define K            (240.0f / 1080.0f)
#define ICON_SCALE   (K * 1.8f)          /* atlas pixels per source pixel (build_collection.py) */
#define CELL         160.0f
#define COLS         6
#define POP_SCALE    1.6f                /* the popup on the top screen: the picture 1.6x the original's size, bigger text */
#define ARROW_L_CELL 21
#define ARROW_R_CELL 22
#define MAX_ITEMS    32
#define MAX_PAGES    8
#define DIM_MAX      0.968627f           /* ColorRect colour at the end of "popup" */
#define HOVER_COLOR  0.9607843f          /* UiButton hover_color = Color.WHITE_SMOKE */

#define Z_SCENE 0.36f
#define Z_ITEM  0.45f
#define Z_BTN   0.47f
#define Z_DIM   0.90f
#define Z_VIS   0.60f
#define Z_TEXT  0.70f

typedef struct {
    int page, slot, item, cell, cw, ch, nw, nh;
    float ox, oy, dim;
    char name[48], desc[200];
    float mod, mod_from, mod_t0, mod_to;   /* UiButton modulate tween (grey level) */
} coll_item_t;

static struct {
    bool open;
    bb_save_t* save;
    C2D_SpriteSheet scene, atlas;
    Tex3DS_SubTexture st;
    coll_item_t it[MAX_ITEMS];
    int n, pages, page_n[MAX_PAGES];
    int page, slot;              /* focus */
    float opened_t;              /* the "hover" loop runs from the scene's start (autoplay) */
    int pop_item;                /* item shown by the popup, -1 none */
    int pop_phase;               /* 0 hidden, 1 "popup", 2 "close" */
    float pop_t0;
} S;

static u32 rgba(u8 r, u8 g, u8 b, float a) { return C2D_Color32(r, g, b, (u8)(a < 0.0f ? 0.0f : (a > 1.0f ? 255.0f : a * 255.0f))); }
static float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
/* Godot ease(x, 0.2) -- an animation key's "transitions" 0.2: 1 - (1 - x)^5 */
static float ease_02(float x) { x = clamp01(x); const float r = 1.0f - x; return 1.0f - r * r * r * r * r; }
/* Godot ease(x, -2): quadratic in-out */
static float ease_io2(float x) { x = clamp01(x); return x < 0.5f ? 2.0f * x * x : 1.0f - 2.0f * (1.0f - x) * (1.0f - x); }
/* Tween TRANS_EXPO EASE_OUT */
static float expo_out(float x) { x = clamp01(x); return x >= 1.0f ? 1.0f : 1.0f - powf(2.0f, -10.0f * x); }

static coll_item_t* item_at(int page, int slot) {
    for (int i = 0; i < S.n; i++) if (S.it[i].page == page && S.it[i].slot == slot) return &S.it[i];
    return NULL;
}
static bool unlocked(const coll_item_t* it) { return it && bb_player_has_item(&S.save->player, it->item); }

/* UiButton.highlight: unlocked items tween to hover / dim over 0.5 s (EXPO out); locked ones stay black */
static void set_target(coll_item_t* it, bool focus) {
    const float to = unlocked(it) ? (focus ? HOVER_COLOR : it->dim) : 0.0f;
    if (to == it->mod_to) return;
    it->mod_from = it->mod;
    it->mod_to = to;
    it->mod_t0 = bb_ui_time();
}

static void focus(int page, int slot, bool sound) {
    coll_item_t* old = item_at(S.page, S.slot);
    coll_item_t* nw = item_at(page, slot);
    if (!nw) return;
    if (old && old != nw) set_target(old, false);
    S.page = page;
    S.slot = slot;
    set_target(nw, true);
    if (sound && unlocked(nw)) bb_uisfx_play(BB_UISFX_TOUCH);   /* collection_item.highlight: sfx_menu_touch, unlocked only */
}

static void turn_page(int dir, int slot) {   /* collection.cycle_pages: wrapi; the sound is the caller's */
    if (S.pages <= 0) return;
    coll_item_t* old = item_at(S.page, S.slot);
    if (old) set_target(old, false);
    S.page = (S.page + dir + S.pages) % S.pages;
    if (slot >= S.page_n[S.page]) slot = S.page_n[S.page] - 1;
    if (slot < 0) slot = 0;
    S.slot = slot;
    coll_item_t* nw = item_at(S.page, S.slot);
    if (nw) set_target(nw, true);
}

static void load_layout(void) {
    S.n = 0;
    S.pages = 0;
    memset(S.page_n, 0, sizeof(S.page_n));
    FILE* f = fopen("romfs:/extras/coll_layout.txt", "r");
    if (!f) return;
    char line[512];
    while (S.n < MAX_ITEMS && fgets(line, sizeof(line), f)) {
        coll_item_t* it = &S.it[S.n];
        memset(it, 0, sizeof(*it));
        char* fld[13];
        int k = 0;
        for (char* p = line; k < 13; ) {
            fld[k++] = p;
            char* bar = strchr(p, '|');
            if (!bar) break;
            *bar = 0;
            p = bar + 1;
        }
        if (k < 13) continue;
        fld[12][strcspn(fld[12], "\r\n")] = 0;
        it->page = atoi(fld[0]); it->slot = atoi(fld[1]); it->item = atoi(fld[2]);
        it->ox = (float)atof(fld[3]); it->oy = (float)atof(fld[4]); it->dim = (float)atof(fld[5]);
        it->cell = atoi(fld[6]); it->cw = atoi(fld[7]); it->ch = atoi(fld[8]); it->nw = atoi(fld[9]); it->nh = atoi(fld[10]);
        snprintf(it->name, sizeof(it->name), "%s", fld[11]);
        snprintf(it->desc, sizeof(it->desc), "%s", fld[12]);
        if (it->page < 0 || it->page >= MAX_PAGES) continue;
        if (it->page + 1 > S.pages) S.pages = it->page + 1;
        if (it->slot + 1 > S.page_n[it->page]) S.page_n[it->page] = it->slot + 1;
        S.n++;
    }
    fclose(f);
}

void bb_coll_open(bb_extras_t* ex, bb_save_t* save) {
    (void)ex;   /* names and full descriptions come from coll_layout.txt (same locale source as bb_extras_t collect) */
    bb_coll_close();
    S.save = save;
    S.scene = bb_extras_load_sheet("coll_scene.t3x");
    S.atlas = bb_extras_load_sheet("coll_items.t3x");
    if (S.scene) C3D_TexSetFilter(C2D_SpriteSheetGetImage(S.scene, 0).tex, GPU_LINEAR, GPU_LINEAR);
    if (S.atlas) C3D_TexSetFilter(C2D_SpriteSheetGetImage(S.atlas, 0).tex, GPU_LINEAR, GPU_LINEAR);
    load_layout();
    for (int i = 0; i < S.n; i++) {   /* collection_item.check_unlock: modulate = dim_color or BLACK */
        coll_item_t* it = &S.it[i];
        it->mod = it->mod_from = it->mod_to = unlocked(it) ? it->dim : 0.0f;
        it->mod_t0 = -10.0f;
    }
    S.page = 0;
    S.slot = 0;
    S.pop_item = -1;
    S.pop_phase = 0;
    S.opened_t = bb_ui_time();
    S.open = true;
    focus(0, 0, false);
}

void bb_coll_close(void) {
    if (S.scene) C2D_SpriteSheetFree(S.scene);
    if (S.atlas) C2D_SpriteSheetFree(S.atlas);
    S.scene = S.atlas = NULL;
    S.open = false;
    S.pop_phase = 0;
}

bool bb_coll_popup_open(void) { return S.open && S.pop_phase == 1; }

static void popup(coll_item_t* it) {   /* collection.show_item -> collection_popup.popup */
    if (!unlocked(it)) { bb_uisfx_play(BB_UISFX_LOCK); return; }
    S.pop_item = (int)(it - S.it);
    S.pop_phase = 1;
    S.pop_t0 = bb_ui_time();
    bb_uisfx_play(COLL_POPUP_SFX);
}

/* Focus the slot holding `item` (smoke scripts); false if the book has no such item. */
bool bb_coll_focus_item(int item) {
    for (int i = 0; i < S.n; i++) if (S.it[i].item == item) { S.page = S.it[i].page; focus(S.it[i].page, S.it[i].slot, false); return true; }
    return false;
}

static bool g_coll_dirty = false;
bool bb_coll_take_dirty(void) { const bool d = g_coll_dirty; g_coll_dirty = false; return d; }

static void close_popup(void) {   /* collection_popup.close: only while previewing */
    if (S.pop_phase != 1) return;
    S.pop_phase = 2;
    S.pop_t0 = bb_ui_time();
    /* _check_level_unlock: closing Cathy's Dorm Key unlocks EGG_DORM_KEY_READ (901), After Class's level_prereq */
    if (S.pop_item >= 0 && S.save && bb_collectable_read(S.save, S.it[S.pop_item].item)) {
        g_coll_dirty = true;
        bb_uisfx_play(BB_UISFX_UNLOCK);
    }
}

/* Focus neighbours of every Collection.tscn page (slots 0-2 = left column top/mid/bottom, 3-5 = right column):
 * up/down cycle a column (0 -> 2 -> 1 -> 0 going up), right/left cross to the same row, outer edges go to the page buttons. */
static void move(int dx, int dy) {
    const int n = S.page_n[S.page];
    int col = S.slot / 3, row = S.slot % 3;
    if (dy) {
        int rows = n - col * 3;
        if (rows > 3) rows = 3;
        if (rows <= 0) return;
        row = (row + dy + rows) % rows;
        focus(S.page, col * 3 + row, true);
        return;
    }
    if (dx > 0 && col == 0 && 3 + row < n) { focus(S.page, 3 + row, true); return; }
    if (dx < 0 && col == 1) { focus(S.page, row, true); return; }
    /* past the outer column: the original focuses PageLeft / PageRightButton; the port turns the page right away */
    turn_page(dx, dx > 0 ? row : (3 + row < S.page_n[(S.page + S.pages - 1) % S.pages] ? 3 + row : row));
    bb_uisfx_play(BB_UISFX_PAGE_TURN);
    coll_item_t* nw = item_at(S.page, S.slot);
    if (nw && unlocked(nw)) bb_uisfx_play(BB_UISFX_TOUCH);
}

int bb_coll_update(u32 kDown, u32 kRep, bool tap, int tx, int ty) {
    if (!S.open) return 1;
    if (S.pop_phase == 1) {   /* previewing: back, accept or a tap anywhere closes */
        if ((kDown & (KEY_B | KEY_A)) || tap) close_popup();
        return 0;
    }
    if (S.pop_phase == 2 && bb_ui_time() - S.pop_t0 >= 0.5f) { S.pop_phase = 0; S.pop_item = -1; }   /* "close": hide at 0.5 s */
    if (kDown & KEY_B) { bb_uisfx_play(BB_UISFX_BACK); return 1; }   /* _on_back_button_button_up */
    if (kDown & (KEY_L | KEY_R)) {   /* left / right bumper */
        turn_page((kDown & KEY_R) ? 1 : -1, S.slot);
        bb_uisfx_play(BB_UISFX_PAGE_TURN);
        return 0;
    }
    if (kRep & (KEY_DUP | KEY_CPAD_UP)) move(0, -1);
    else if (kRep & (KEY_DDOWN | KEY_CPAD_DOWN)) move(0, 1);
    else if (kRep & (KEY_DLEFT | KEY_CPAD_LEFT)) move(-1, 0);
    else if (kRep & (KEY_DRIGHT | KEY_CPAD_RIGHT)) move(1, 0);
    if (kDown & KEY_A) { coll_item_t* it = item_at(S.page, S.slot); if (it) popup(it); }
    if (tap) {
        const int z = bb_ui_zone_hit(tx, ty);
        if (z == BB_ZONE_BACK) { bb_uisfx_play(BB_UISFX_BACK); return 1; }
        if (z == BB_ZONE_PREV || z == BB_ZONE_NEXT) { turn_page(z == BB_ZONE_NEXT ? 1 : -1, S.slot); bb_uisfx_play(BB_UISFX_PAGE_TURN); }
        else if (z >= BB_ZONE_LEVEL && z < BB_ZONE_LEVEL + 6) {   /* select, then open */
            const int s = z - BB_ZONE_LEVEL;
            if (s == S.slot) { coll_item_t* it = item_at(S.page, s); if (it) popup(it); }
            else focus(S.page, s, true);
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- drawing */
static C2D_Image cell_image(int cell, float w, float h) {
    C2D_Image img = C2D_SpriteSheetGetImage(S.atlas, 0);
    const float tw = (float)img.tex->width, th = (float)img.tex->height;
    const float x = (float)(cell % COLS) * CELL, y = (float)(cell / COLS) * CELL;
    S.st.width = (u16)w; S.st.height = (u16)h;
    S.st.left = x / tw; S.st.right = (x + w) / tw;
    S.st.top = 1.0f - y / th; S.st.bottom = 1.0f - (y + h) / th;
    img.subtex = &S.st;
    return img;
}

static void draw_cell(int cell, float cw, float ch, float cx, float cy, float w, float h, u32 tint, float z) {
    if (!S.atlas) return;
    C2D_Image img = cell_image(cell, cw, ch);
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, tint, 1.0f);
    C2D_SetTintMode(C2D_TintMult);
    C2D_DrawParams dp = { { cx - w * 0.5f, cy - h * 0.5f, w, h }, { 0.0f, 0.0f }, z, 0.0f };
    C2D_DrawImage(img, &dp, &t);
}

/* Collection.tscn page space -> bottom screen: Pages sit at (-960,-540) inside the Book (960,500, scale 0.9). The whole book is shown:
 * scene x 120..1800 at SC = 320/1680, 10 px down (build_collection.py CROP_X / OFF_Y); the background alone covers the rest. */
#define SC    (320.0f / 1680.0f)
#define OFF_Y 10.0f
static float bx(float page_x) { return (960.0f + 0.9f * (page_x - 960.0f) - 120.0f) * SC; }
static float by(float page_y) { return (500.0f + 0.9f * (page_y - 540.0f)) * SC + OFF_Y; }

static void page_button(int cell, float cx, float cy, int zone) {
    /* generic_text_button with StyleBoxFlat_4y0lg: 0.0625 grey, radius 10, expand margin 20 around the 64 px icon (scaled 0.9 * K) */
    const float s = (64.0f + 40.0f) * 0.9f * SC, r = 10.0f * 0.9f * SC + 1.0f, ic = 64.0f * 0.9f * SC;
    const u32 bg = C2D_Color32(0x10, 0x10, 0x10, 0xFF);
    C2D_DrawRectSolid(cx - s * 0.5f + r, cy - s * 0.5f, Z_BTN, s - 2.0f * r, s, bg);
    C2D_DrawRectSolid(cx - s * 0.5f, cy - s * 0.5f + r, Z_BTN, s, s - 2.0f * r, bg);
    for (int q = 0; q < 4; q++) C2D_DrawCircleSolid(cx + ((q & 1) ? 1.0f : -1.0f) * (s * 0.5f - r), cy + ((q & 2) ? 1.0f : -1.0f) * (s * 0.5f - r), Z_BTN, r, bg);
    draw_cell(cell, 32.0f, 32.0f, cx, cy, ic + 2.0f, ic + 2.0f, 0xFFFFFFFFu, Z_BTN + 0.01f);
    bb_ui_zone_add(zone, cx - 16.0f, cy - 20.0f, 32.0f, 40.0f);   /* a finger-sized target around the small button */
}

void bb_coll_draw_bottom(void) {
    if (!S.open) return;
    C2D_DrawRectSolid(0, 0, Z_SCENE - 0.01f, 320, 240, C2D_Color32(0x10, 0x10, 0x10, 0xFF));
    if (S.scene) {
        C2D_Image img = C2D_SpriteSheetGetImage(S.scene, 0);
        static Tex3DS_SubTexture st;
        st.width = 320; st.height = 240;
        st.left = 0.0f; st.right = 320.0f / (float)img.tex->width;
        st.top = 1.0f; st.bottom = 1.0f - 240.0f / (float)img.tex->height;
        img.subtex = &st;
        C2D_DrawImageAt(img, 0.0f, 0.0f, Z_SCENE, NULL, 1.0f, 1.0f);
    }
    const float now = bb_ui_time();
    for (int i = 0; i < S.n; i++) {
        coll_item_t* it = &S.it[i];
        it->mod = it->mod_from + (it->mod_to - it->mod_from) * expo_out((now - it->mod_t0) / 0.5f);
        if (it->page != S.page) continue;
        /* CollectionItem 384x384; its TextureRect fills it less 24 px right / bottom, KEEP_ASPECT_CENTERED */
        const float box = 360.0f * 0.9f * SC, x0 = bx(it->ox), y0 = by(it->oy);
        const float f = box / (float)(it->nw > it->nh ? it->nw : it->nh);
        float v = it->mod;
        if (!unlocked(it) && it->slot == S.slot) v = 0.16f;   /* port: the focus on a locked (black) item stays visible */
        const u8 g = (u8)(255.0f * clamp01(v));
        draw_cell(it->cell, (float)it->cw, (float)it->ch, x0 + box * 0.5f, y0 + box * 0.5f, (float)it->nw * f, (float)it->nh * f,
                  C2D_Color32(g, g, g, 0xFF), Z_ITEM);
        bb_ui_zone_add(BB_ZONE_LEVEL + it->slot, x0, y0, 384.0f * 0.9f * SC, 384.0f * 0.9f * SC);
    }
    /* PageControl's arrows sit outside the 4:3 crop (book x -872 / +872); the port keeps them at the screen edges, between the rows */
    page_button(ARROW_L_CELL, 11.0f, by(540.0f), BB_ZONE_PREV);
    page_button(ARROW_R_CELL, 309.0f, by(540.0f), BB_ZONE_NEXT);
    bb_art_icon(7, 22.0f, 216.0f, 26.0f, 0xFFFFFFFFu, Z_BTN);   /* BackButton (HotMenu back_button) */
    bb_ui_zone_add(BB_ZONE_BACK, 4.0f, 198.0f, 40.0f, 40.0f);

    /* the popup's ColorRect, here over the bottom screen only */
    float dim = 0.0f;
    if (S.pop_phase == 1) dim = DIM_MAX * ease_02((now - S.pop_t0) / 0.4f);
    else if (S.pop_phase == 2) dim = DIM_MAX * (1.0f - ease_02((now - S.pop_t0) / 0.5f));
    if (dim > 0.0f) C2D_DrawRectSolid(0, 0, Z_DIM, 320, 240, rgba(0, 0, 0, dim));
}

static int wrap13(const char* s, float maxw, char out[][96], int max_lines) {
    int n = 0;
    while (*s && n < max_lines) {
        while (*s == ' ') s++;
        int len = (int)strlen(s), cut = len, last = -1;
        char tmp[96];
        for (int i = 0; i < len && i < 94; i++) {
            snprintf(tmp, sizeof(tmp), "%.*s", i + 1, s);
            if (bb_font_width(BB_FACE_BOLD, 13, tmp) > maxw) { cut = last > 0 ? last : i; break; }
            if (s[i] == ' ') last = i;
            if (i == 93) cut = last > 0 ? last : 94;
        }
        snprintf(out[n++], 96, "%.*s", cut, s);
        s += cut;
    }
    return n;
}

void bb_coll_draw_top(void) {
    bb_view_draw_checker(0x30);   /* the standard checkerboard */
    if (!S.open || S.pop_phase == 0 || S.pop_item < 0) return;
    const float now = bb_ui_time(), t = now - S.pop_t0;
    if (S.pop_phase == 2 && t >= 0.5f) return;
    const coll_item_t* it = &S.it[S.pop_item];
    float vis_a, vis_y, lore_a, lore_y;
    if (S.pop_phase == 1) {   /* "popup" (0.8 s): keys at 0 -> 0.8 / 0.6 / 0.1-0.6 / 0.1-0.5, all with transition 0.2 */
        vis_a = ease_02(t / 0.8f);
        vis_y = 400.0f + (367.0f - 400.0f) * ease_02(t / 0.6f);
        lore_a = t < 0.1f ? 0.0f : ease_02((t - 0.1f) / 0.5f);
        lore_y = t < 0.1f ? 700.0f : 700.0f + (660.0f - 700.0f) * ease_02((t - 0.1f) / 0.4f);
    } else {                  /* "close" (0.5 s), then hide */
        const float k = ease_02(t / 0.5f);
        vis_a = 1.0f - k;
        vis_y = 367.0f + (400.0f - 367.0f) * k;
        lore_a = 1.0f - k;
        lore_y = 660.0f + (700.0f - 660.0f) * k;
    }
    /* AnimationPlayer2 "hover" (autoplay, 4 s loop): Visual scale 1 -> 1.1 -> 1, transitions -2 */
    const float ph = fmodf(now - S.opened_t, 4.0f);
    const float hover = 1.0f + 0.1f * (ph < 2.0f ? ease_io2(ph / 2.0f) : 1.0f - ease_io2((ph - 2.0f) / 2.0f));
    /* top screen = the 1800 px centre of 1920 at K (x 60..1860) */
    const float cx = (960.0f - 60.0f) * K;
    const float vis_cy = 80.0f + (vis_y - 367.0f) * K;   /* the original's rise, around a higher centre for the larger picture */
    float vw = (float)it->nw * K * POP_SCALE * hover, vh = (float)it->nh * K * POP_SCALE * hover;
    if (vh > 150.0f) { vw *= 150.0f / vh; vh = 150.0f; }
    draw_cell(it->cell, (float)it->cw, (float)it->ch, cx, vis_cy, vw, vh, C2D_Color32(0xFF, 0xFF, 0xFF, (u8)(255.0f * clamp01(vis_a))), Z_VIS);
    if (lore_a <= 0.0f || !bb_font_ready()) return;
    /* LoreText: Label (Encode Sans SC Condensed ExtraBold 80, centred in -153..154 x -29..93) and the RichTextLabel below it
     * (Encode Sans SC Bold 40, grey 0.4728, centred, 1582 px wide from y +88) */
    const u32 white = rgba(0xFF, 0xFF, 0xFF, lore_a), grey = rgba(0x79, 0x79, 0x79, lore_a);
    const float dy = (lore_y - 660.0f) * K;   /* the LoreText rise */
    bb_font_draw(BB_FACE_BOLD, 26, cx + 1.5f, 160.0f + dy + 1.5f, rgba(0, 0, 0, 0.8f * lore_a), C2D_AlignCenter, it->name, Z_TEXT - 0.01f);
    bb_font_draw(BB_FACE_BOLD, 26, cx, 160.0f + dy, white, C2D_AlignCenter, it->name, Z_TEXT);
    char lines[4][96];
    const int n = wrap13(it->desc, 372.0f, lines, 3);
    const float lh = (float)bb_font_line_height(BB_FACE_BOLD, 13);
    for (int i = 0; i < n; i++) bb_font_draw(BB_FACE_BOLD, 13, cx, 192.0f + dy + lh * (float)i, grey, C2D_AlignCenter, lines[i], Z_TEXT);
}
