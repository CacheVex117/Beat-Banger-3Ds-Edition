/**
 * @file music_ui.c
 * @brief Extras > Music: the Sound Test (see music_ui.h).
 */
#include "music_ui.h"
#include "menu_ui.h"
#include "game_view.h"
#include "ui_motion.h"
#include "ui_sfx.h"
#include "ui_art.h"
#include "font.h"
#include "extras.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define ZS 0.30f   /* studio layers */
#define ZP 0.35f
#define ZI 0.45f
#define ZT 0.50f
#define N_REC 10
#define HEADPHONES 702   /* gacha_cathy_headphones (sound_test.check_unlocks) */

/* sound_test.tscn records: title, record colour, bpm, unlock item (shop Music). The file is romfs:/music/st_<k>.wav (build_viewers.py). */
static const struct { const char* title; u8 r, g, b; int bpm; int item; } k_rec[N_REC] = {
    { "Pre Alpha Theme", 0xCA, 0x21, 0x30, 128, 201 }, { "1.0 New Theme", 0xCA, 0x21, 0x30, 128, 202 }, { "2.0 Theme", 0xCA, 0x21, 0x30, 126, 203 },
    { "Legacy Bunny", 0x5E, 0x7E, 0x59, 128, 204 }, { "Legacy Goat", 0x5E, 0x7E, 0x59, 128, 205 }, { "Legacy Dinosaur", 0x5E, 0x7E, 0x59, 140, 206 },
    { "Legacy Wolf", 0x5E, 0x7E, 0x59, 126, 207 }, { "Cat Jam", 0x17, 0xA6, 0xFF, 140, 297 }, { "BB PS1 JUNGLE", 0xFF, 0xC9, 0x17, 160, 298 },
    { "I Didn't Sleep", 0xFF, 0xC9, 0x17, 150, 299 },
};

static struct {
    bool open, dirty;
    bb_save_t* save;
    int cursor;        /* MenuCache.sound_test_menu_index: remembered between visits */
    int playing;       /* -1 = nothing */
    char path[40];
    int64_t pos_us, dur_us;
    int last_beat;
    float beat_t;      /* the last beat (speaker / record pulses) */
    float spin;        /* the selected record's rotation */
    float scroll;      /* carousel position (smoothed towards the cursor) */
    C2D_SpriteSheet base, inside, front, record;
} M = { .playing = -1, .last_beat = -1, .beat_t = -10.0f };

static u32 rgba(u8 r, u8 g, u8 b, float a) { return C2D_Color32(r, g, b, (u8)(a < 0.0f ? 0.0f : (a > 1.0f ? 255.0f : a * 255.0f))); }
static bool owned(int k) { return M.save && bb_player_has_item(&M.save->player, k_rec[k].item); }

void bb_music_open(bb_save_t* save) {
    bb_music_close();
    M.open = true;
    M.save = save;
    M.playing = -1;   /* sound_test._ready: SoundManager.stop_all_music() */
    M.pos_us = M.dur_us = 0;
    M.last_beat = -1;
    M.scroll = (float)M.cursor;
    M.base = bb_extras_load_sheet("st_base.t3x");
    M.inside = bb_extras_load_sheet("st_inside.t3x");
    M.front = bb_extras_load_sheet("st_front.t3x");
    M.record = bb_extras_load_sheet("st_record.t3x");
    C2D_SpriteSheet all[4] = { M.base, M.inside, M.front, M.record };
    for (int i = 0; i < 4; i++) if (all[i]) C3D_TexSetFilter(C2D_SpriteSheetGetImage(all[i], 0).tex, GPU_LINEAR, GPU_LINEAR);
    bool all_owned = true;   /* check_unlocks: every record owned -> Cathy's Headphones */
    for (int k = 0; k < N_REC; k++) if (!owned(k)) all_owned = false;
    if (all_owned && save && bb_player_add_item(&save->player, HEADPHONES)) M.dirty = true;
}

void bb_music_close(void) {
    C2D_SpriteSheet* all[4] = { &M.base, &M.inside, &M.front, &M.record };
    for (int i = 0; i < 4; i++) if (*all[i]) { C2D_SpriteSheetFree(*all[i]); *all[i] = NULL; }
    M.open = false;
    M.playing = -1;
}

bool bb_music_take_dirty(void) { const bool d = M.dirty; M.dirty = false; return d; }

const char* bb_music_want(void) {
    if (!M.open || M.playing < 0) return NULL;
    snprintf(M.path, sizeof(M.path), "romfs:/music/st_%d.wav", M.playing);
    return M.path;
}

void bb_music_set_pos(int64_t pos_us, int64_t dur_us) {
    M.pos_us = pos_us < 0 ? 0 : pos_us;
    M.dur_us = dur_us;
    if (M.playing < 0) return;
    const int beat = (int)((double)M.pos_us / 1e6 * (double)k_rec[M.playing].bpm / 60.0);   /* EventManager.menu_beat */
    if (beat != M.last_beat) { M.last_beat = beat; M.beat_t = bb_ui_time(); }
}

static void rec_select(int k) {   /* Menu cursor: ui_tick like the BaseMenu cursor */
    if (k < 0) k = N_REC - 1;
    if (k >= N_REC) k = 0;
    if (k != M.cursor) { M.cursor = k; M.spin = 0.0f; bb_uisfx_play(BB_UISFX_TICK); }
}

int bb_music_update(u32 kDown, u32 kRep, bool tap, int tx, int ty) {
    if (!M.open) return 1;
    if (tap) {
        const int z = bb_ui_zone_hit(tx, ty);
        if (z == BB_ZONE_BACK) kDown |= KEY_B;
        else if (z == BB_ZONE_PREV) rec_select(M.cursor - 1);
        else if (z == BB_ZONE_NEXT) rec_select(M.cursor + 1);
        else if (z >= BB_ZONE_LEVEL && z < BB_ZONE_LEVEL + N_REC) {   /* Record: a tap on the selected record plays it */
            if (z - BB_ZONE_LEVEL == M.cursor) kDown |= KEY_A;
            else rec_select(z - BB_ZONE_LEVEL);
        }
    }
    if (kRep & (KEY_DLEFT | KEY_CPAD_LEFT | KEY_L)) rec_select(M.cursor - 1);
    if (kRep & (KEY_DRIGHT | KEY_CPAD_RIGHT | KEY_R)) rec_select(M.cursor + 1);
    if (kDown & KEY_B) { bb_uisfx_play(BB_UISFX_BACK); return 1; }   /* back to Extras; the record keeps playing there (stop_music false) */
    if (kDown & KEY_A) {   /* _on_song_selected */
        if (!owned(M.cursor)) bb_uisfx_play(BB_UISFX_LOCK);
        else { M.playing = M.cursor; M.pos_us = 0; M.dur_us = 0; M.last_beat = -1; }
    }
    if (kDown & KEY_X) M.playing = -1;   /* port: X stops the record */
    return 0;
}

/* ---------------------------------------------------------------- top: the studio monitor */
static void layer(C2D_SpriteSheet sh, float cx, float cy, float s, float z) {
    if (!sh) return;
    C2D_Image img = C2D_SpriteSheetGetImage(sh, 0);
    Tex3DS_SubTexture st = { 400, 240, 0.0f, 1.0f, 400.0f / 512.0f, 1.0f - 240.0f / 256.0f };
    img.subtex = &st;
    C2D_DrawParams dp = { { cx - 200.0f * s, cy - 120.0f * s, 400.0f * s, 240.0f * s }, { 0, 0 }, z, 0.0f };
    C2D_DrawImage(img, &dp, NULL);
}

static float beat_k(float dur) {   /* 0 at the beat -> 1 after dur */
    const float e = (bb_ui_time() - M.beat_t) / dur;
    return e < 0.0f ? 1.0f : (e > 1.0f ? 1.0f : e);
}

void bb_music_draw_top(void) {
    bb_view_draw_checker(0x40);
    C2D_DrawRectSolid(0, 0, ZS - 0.02f, 400, 240, rgba(0, 0, 0, 0.35f));
    /* UiStudioSpeakersInside: 1.01 on the beat, back to 1.0 over 0.2 s (bounce) */
    const float sp = M.playing >= 0 ? 1.0f + 0.01f * (1.0f - bb_ui_ease_out(beat_k(0.2f))) : 1.0f;
    layer(M.base, 200.0f, 120.0f, 1.0f, ZS);
    layer(M.inside, 200.0f, 120.0f, sp, ZS + 0.01f);
    layer(M.front, 200.0f, 120.0f, 1.0f, ZS + 0.02f);
    if (!bb_font_ready()) return;
    const float a = bb_ui_appear(0.0f, 0.4f);
    char buf[48];
    bb_font_draw(BB_FACE_BOLD, 10, 92.0f, 54.0f, rgba(0x9A, 0x9A, 0xA6, a), C2D_AlignLeft, "NOW PLAYING", ZT);
    snprintf(buf, sizeof(buf), "%.30s", M.playing >= 0 ? k_rec[M.playing].title : "NONE");
    bb_font_draw(BB_FACE_NOTO_BLACK, 20, 92.0f, 66.0f, rgba(0xFF, 0xFF, 0xFF, a), C2D_AlignLeft, buf, ZT);
    snprintf(buf, sizeof(buf), "%d BPM", M.playing >= 0 ? k_rec[M.playing].bpm : 0);
    bb_font_draw(BB_FACE_BOLD, 12, 310.0f, 54.0f, rgba(0xFF, 0xC8, 0x3C, a), C2D_AlignRight, buf, ZT);
    if (M.playing >= 0) {   /* an equaliser that jumps on the beat, between the title and the bar */
        const float k = 1.0f - bb_ui_ease_out(beat_k(60.0f / (float)k_rec[M.playing].bpm));
        for (int i = 0; i < 18; i++) {
            const float h = 8.0f + 30.0f * k * (0.35f + 0.65f * fabsf(sinf((float)i * 1.7f + (float)M.last_beat * 0.9f)));
            C2D_DrawRectSolid(100.0f + (float)i * 11.5f, 166.0f - h, ZT - 0.02f, 8.0f, h, rgba(k_rec[M.playing].r, k_rec[M.playing].g, k_rec[M.playing].b, 0.8f * a));
        }
    }
    /* SongProgressBar */
    const float f = M.dur_us > 0 ? (float)((double)M.pos_us / (double)M.dur_us) : 0.0f;
    C2D_DrawRectSolid(100.0f, 176.0f, ZT - 0.01f, 202.0f, 4.0f, rgba(0x30, 0x30, 0x38, a));
    C2D_DrawRectSolid(100.0f, 176.0f, ZT, 202.0f * (f > 1.0f ? 1.0f : f), 4.0f, rgba(0xFF, 0xFF, 0xFF, a));
    if (M.dur_us > 0) {
        const int ps = (int)(M.pos_us / 1000000), ds = (int)(M.dur_us / 1000000);
        snprintf(buf, sizeof(buf), "%d:%02d / %d:%02d", ps / 60, ps % 60, ds / 60, ds % 60);
        bb_font_draw(BB_FACE_BODY, 10, 302.0f, 182.0f, rgba(0x9A, 0x9A, 0xA6, a), C2D_AlignRight, buf, ZT);
    }
}

/* ---------------------------------------------------------------- bottom: the record carousel */
static void rec_cell(int cell, float cx, float cy, float size, float angle, u32 tint, float z) {
    if (!M.record) return;
    C2D_Image img = C2D_SpriteSheetGetImage(M.record, 0);
    Tex3DS_SubTexture st = { 128, 128, (float)cell * 128.0f / 512.0f, 1.0f, (float)(cell + 1) * 128.0f / 512.0f, 0.0f };
    img.subtex = &st;
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, tint, 1.0f);
    C2D_SetTintMode(C2D_TintMult);
    C2D_DrawParams dp = { { cx, cy, size, size }, { size * 0.5f, size * 0.5f }, z, angle };
    C2D_DrawImage(img, &dp, &t);
}

void bb_music_draw_bottom(void) {
    bb_view_draw_checker(0x60);
    const float a = bb_ui_appear(0.0f, 0.35f);
    if (bb_font_ready()) bb_font_draw_smallcaps(BB_FACE_BOLD, 21, 160.0f, 6.0f, rgba(0xFF, 0xFF, 0xFF, a), C2D_AlignCenter, "Music", ZT);
    bb_ui_smooth(&M.scroll, (float)M.cursor, 12.0f);
    M.spin += 1.0f / 60.0f;   /* Record._process: rotate(1 * delta) while selected */
    /* Record._on_menu_beat: every record 1.0 -> 0.9 over 0.4 s (back ease) */
    const float bp = M.playing >= 0 ? 1.0f - 0.1f * bb_ui_ease_out_back(beat_k(0.4f)) : 1.0f;
    for (int k = 0; k < N_REC; k++) {
        const float d = (float)k - M.scroll;
        if (fabsf(d) > 2.6f) continue;
        const bool sel = k == M.cursor, own = owned(k);
        const float size = (sel ? 112.0f : 72.0f) * (M.playing >= 0 ? bp : 1.0f);
        const float cx = 160.0f + d * 104.0f, cy = 104.0f;
        const float v = sel ? 1.0f : 0.2f;   /* highlight: modulate 1 / 0.2 */
        const u32 tw = C2D_Color32((u8)(255 * v), (u8)(255 * v), (u8)(255 * v), (u8)(255.0f * a));
        if (sel) rec_cell(2, cx, cy, size * 1.12f, 0.0f, C2D_Color32(0xFF, 0xFF, 0xFF, (u8)(255.0f * a)), ZP);   /* Outline */
        rec_cell(0, cx, cy, size, sel ? M.spin : 0.0f, tw, ZI);
        rec_cell(1, cx, cy, size, sel ? M.spin : 0.0f,
                 C2D_Color32((u8)(k_rec[k].r * v), (u8)(k_rec[k].g * v), (u8)(k_rec[k].b * v), (u8)(255.0f * a)), ZI + 0.01f);
        if (!own) {   /* Locked: an alpha circle and the lock */
            rec_cell(3, cx, cy, size * 0.6f, 0.0f, C2D_Color32(0, 0, 0, (u8)(200.0f * a)), ZI + 0.02f);
            C2D_DrawRectSolid(cx - 7.0f, cy - 2.0f, ZI + 0.03f, 14.0f, 11.0f, rgba(0xC8, 0xC8, 0xD2, a));
            C2D_DrawCircleSolid(cx, cy - 4.0f, ZI + 0.025f, 5.0f, rgba(0xC8, 0xC8, 0xD2, a));
            C2D_DrawCircleSolid(cx, cy - 4.0f, ZI + 0.026f, 3.0f, rgba(0, 0, 0, a));
        }
        if (M.playing == k) C2D_DrawCircleSolid(cx + size * 0.38f, cy - size * 0.38f, ZI + 0.03f, 4.0f, rgba(0x4A, 0xE0, 0x6A, a));
        bb_ui_zone_add(BB_ZONE_LEVEL + k, cx - size * 0.5f, cy - size * 0.5f, size, size);
    }
    if (bb_font_ready()) {   /* BannerLabel of the selected record */
        char buf[48];
        snprintf(buf, sizeof(buf), "%.30s", owned(M.cursor) ? k_rec[M.cursor].title : "???");
        bb_font_draw(BB_FACE_HAND, 22, 160.0f, 166.0f, rgba(0xFF, 0xFF, 0xFF, a), C2D_AlignCenter, buf, ZT);
        bb_font_draw(BB_FACE_BODY, 10, 160.0f, 194.0f, rgba(0x9A, 0x9A, 0xA6, a), C2D_AlignCenter,
                     owned(M.cursor) ? (M.playing == M.cursor ? "Playing   X: stop" : "A / tap: play") : "Buy it in Jasmine's Shop", ZT);
        const float arrow = 3.0f * sinf(bb_ui_time() * 6.0f);
        bb_font_draw(BB_FACE_BOLD, 26, 18.0f - arrow, 88.0f, rgba(0xFF, 0xFF, 0xFF, 0.85f * a), C2D_AlignCenter, "<", ZT);
        bb_font_draw(BB_FACE_BOLD, 26, 302.0f + arrow, 88.0f, rgba(0xFF, 0xFF, 0xFF, 0.85f * a), C2D_AlignCenter, ">", ZT);
    }
    bb_ui_zone_add(BB_ZONE_PREV, 0, 60.0f, 36.0f, 90.0f);
    bb_ui_zone_add(BB_ZONE_NEXT, 284.0f, 60.0f, 36.0f, 90.0f);
    bb_art_icon(7, 22.0f, 216.0f, 26.0f, C2D_Color32(255, 255, 255, (u8)(255.0f * a)), ZT);
    bb_ui_zone_add(BB_ZONE_BACK, 4.0f, 198.0f, 36.0f, 38.0f);
}
