/**
 * @file cutscene.c
 * @brief Cutscene player (see cutscene.h).
 */
#include "cutscene.h"
#include "game_view.h"
#include "font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PANELS 64
/* dialogue box: name band (cutscene_nameplate) + dialogue lines, overlaid on the bottom of the full-size picture */
#define CS_PLATE_H 17.0f
#define CS_LINE_H  15.0f
#define CS_TEXT_W  372.0f
#define MAX_CLIPS  64
#define MAX_SEGS   24
#define MAX_CHARS  8
#define MAX_TRACKS 4

#define CH_VOICE 3
#define CH_SFX0  4
#define CH_SFX1  5
#define CH_TRACK0 6

typedef struct {
    int  image;
    char character[24];
    char text[240];
    int  voice;               /* clip index or -1 */
    int  sfx[4];
    int  sfx_n;
    bool fade;
} panel_t;

typedef struct { char name[32]; uint32_t offset, frames, rate; } clip_t;
typedef struct { int clip, start, end; bool bus_music, restart, fade_in, fade_out; } seg_t;
typedef struct { char name[24]; u8 r, g, b; } color_t;
typedef struct {
    int clip;                 /* the track's clip */
    int slot;                 /* NDSP channel slot */
    bool started, active, bus_music;
    float vol, target;
    ndspWaveBuf wb;
} track_t;

static struct {
    bool open, ndsp, finished;
    char dir[64], which[8];
    int panel_count, panel, clip_count, seg_count, color_count, track_count;
    panel_t panels[MAX_PANELS];
    clip_t clips[MAX_CLIPS];
    seg_t segs[MAX_SEGS];
    color_t colors[MAX_CHARS];
    track_t tracks[MAX_TRACKS];
    int16_t* pcm;
    uint32_t pcm_bytes;
    ndspWaveBuf wb_voice, wb_sfx[2];
    int sfx_rr;
    C2D_SpriteSheet image;
    int image_idx;
    float panel_t_s;
    float vol_master, vol_music, vol_sfx, vol_voice;
} cs;

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static void chan_init(int ch) {
    ndspChnReset(ch);
    ndspChnInitParams(ch);
    ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
    ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
}

static void chan_mix(int ch, float v) {
    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = mix[1] = v < 0.001f ? 0.001f : v;   /* never an all-zero mix */
    ndspChnSetMix(ch, mix);
}

static void play_clip(int ch, ndspWaveBuf* wb, int clip, bool loop) {
    if (!cs.ndsp || !cs.pcm || clip < 0 || clip >= cs.clip_count || cs.clips[clip].frames == 0) {
        return;
    }
    ndspChnWaveBufClear(ch);
    ndspChnSetRate(ch, (float)cs.clips[clip].rate);
    memset(wb, 0, sizeof(*wb));
    wb->data_vaddr = (const u8*)cs.pcm + cs.clips[clip].offset;
    wb->nsamples = cs.clips[clip].frames;
    wb->looping = loop;
    ndspChnWaveBufAdd(ch, wb);
}

static int find_clip(const char* name) {
    if (!name[0]) return -1;
    for (int i = 0; i < cs.clip_count; i++) if (strcmp(cs.clips[i].name, name) == 0) return i;
    return -1;
}

static int track_for_clip(int clip, bool music) {
    for (int i = 0; i < cs.track_count; i++) if (cs.tracks[i].clip == clip) return i;
    if (cs.track_count >= MAX_TRACKS) return -1;
    track_t* t = &cs.tracks[cs.track_count];
    memset(t, 0, sizeof(*t));
    t->clip = clip;
    t->slot = cs.track_count;
    t->bus_music = music;
    return cs.track_count++;
}

static void apply_volumes(void) {
    if (!cs.ndsp) return;
    chan_mix(CH_VOICE, cs.vol_master * cs.vol_voice);
    chan_mix(CH_SFX0, cs.vol_master * cs.vol_sfx);
    chan_mix(CH_SFX1, cs.vol_master * cs.vol_sfx);
    for (int i = 0; i < cs.track_count; i++) {
        const track_t* t = &cs.tracks[i];
        chan_mix(CH_TRACK0 + t->slot, cs.vol_master * (t->bus_music ? cs.vol_music : cs.vol_sfx) * t->vol);
    }
}

void bb_cs_set_volumes(float master, float music, float sfx, float voice) {
    cs.vol_master = master; cs.vol_music = music; cs.vol_sfx = sfx; cs.vol_voice = voice;
    if (cs.open) apply_volumes();
}

/* Sheets are freed one frame late (bb_cs_flush, called after C3D_FrameBegin): the previous frame may still be drawing from them. */
static C2D_SpriteSheet g_pending[4];
static void defer_free(C2D_SpriteSheet sh) {
    for (int i = 0; i < 4; i++) {
        if (!g_pending[i]) { g_pending[i] = sh; return; }
    }
    C2D_SpriteSheetFree(sh);   /* should not happen; at least never leak */
}

void bb_cs_flush(void) {
    for (int i = 0; i < 4; i++) {
        if (g_pending[i]) { C2D_SpriteSheetFree(g_pending[i]); g_pending[i] = NULL; }
    }
}

static void load_image(int idx) {
    if (idx == cs.image_idx) return;
    if (cs.image) {
        defer_free(cs.image);
        cs.image = NULL;
    }
    cs.image_idx = idx;
    if (idx >= 0) {
        char path[96];
        snprintf(path, sizeof(path), "%s/cutscene/%s_%d.t3x", cs.dir, cs.which, idx);
        cs.image = C2D_SpriteSheetLoad(path);
    }
}

/* Enter panel i: stop the previous panel's sounds, start the new ones, update the global tracks (cutscene_tracks_panel.gd). */
static void enter_panel(int i) {
    cs.panel = i;
    cs.panel_t_s = 0.0f;
    const panel_t* p = &cs.panels[i];
    load_image(p->image);
    if (cs.ndsp) {
        ndspChnWaveBufClear(CH_VOICE);
        ndspChnWaveBufClear(CH_SFX0);
        ndspChnWaveBufClear(CH_SFX1);
        if (p->voice >= 0) play_clip(CH_VOICE, &cs.wb_voice, p->voice, false);
        for (int k = 0; k < p->sfx_n; k++) {
            int ch = (k & 1) ? CH_SFX1 : CH_SFX0;
            play_clip(ch, &cs.wb_sfx[k & 1], p->sfx[k], false);
        }
    }
    for (int t = 0; t < cs.track_count; t++) {
        track_t* tr = &cs.tracks[t];
        const seg_t* seg = NULL;
        for (int s = 0; s < cs.seg_count; s++) {
            if (cs.segs[s].clip == tr->clip && i >= cs.segs[s].start && i <= cs.segs[s].end) seg = &cs.segs[s];
        }
        if (seg) {
            if (!tr->active || seg->restart) {
                if (!tr->started || seg->restart) {
                    play_clip(CH_TRACK0 + tr->slot, &tr->wb, tr->clip, true);
                    tr->started = true;
                }
                tr->vol = seg->fade_in ? 0.0f : 1.0f;
                tr->target = 1.0f;
                tr->active = true;
            }
        } else if (tr->active) {
            tr->active = false;
            tr->target = 0.0f;
            bool fade_out = false;
            for (int s = 0; s < cs.seg_count; s++) if (cs.segs[s].clip == tr->clip && cs.segs[s].end == i - 1) fade_out = cs.segs[s].fade_out;
            if (!fade_out) {   /* the reference pauses the stream; stopping it keeps the DSP voice count low (resume restarts the loop) */
                tr->vol = 0.0f;
                if (cs.ndsp) ndspChnWaveBufClear(CH_TRACK0 + tr->slot);
                tr->started = false;
            }   /* the reference pauses the stream; a silent looping stream is equivalent */
        }
    }
    apply_volumes();
}

bool bb_cs_open(const char* stage_dir, const char* which, bool ndsp_available) {
    memset(&cs, 0, sizeof(cs));
    cs.image_idx = -2;
    cs.vol_master = cs.vol_music = cs.vol_sfx = cs.vol_voice = 1.0f;
    snprintf(cs.dir, sizeof(cs.dir), "%s", stage_dir);
    snprintf(cs.which, sizeof(cs.which), "%s", which);
    cs.ndsp = ndsp_available;

    char path[112];
    snprintf(path, sizeof(path), "%s/cutscene/%s.txt", cs.dir, which);
    FILE* f = fopen(path, "r");
    if (!f) {
        return false;
    }
    /* audio bank first (clip names are referenced by the script) */
    snprintf(path, sizeof(path), "%s/cutscene/%s.bbca", cs.dir, which);
    FILE* bf = fopen(path, "rb");
    if (bf) {
        uint8_t hdr[16];
        if (fread(hdr, 1, 12, bf) == 12 && memcmp(hdr, "BBCA", 4) == 0 && rd32(hdr + 4) == 1 && rd32(hdr + 8) <= MAX_CLIPS) {
            int n = (int)rd32(hdr + 8);
            uint8_t ent[44];
            bool ok = true;
            for (int i = 0; i < n && ok; i++) {
                if (fread(ent, 1, 44, bf) != 44) { ok = false; break; }
                memcpy(cs.clips[i].name, ent, 31);
                cs.clips[i].offset = rd32(ent + 32);
                cs.clips[i].frames = rd32(ent + 36);
                cs.clips[i].rate = rd32(ent + 40);
            }
            if (ok) {
                cs.clip_count = n;
                long here = ftell(bf);
                fseek(bf, 0, SEEK_END);
                long total = ftell(bf);
                fseek(bf, here, SEEK_SET);
                cs.pcm_bytes = (uint32_t)(total - here);
                if (cs.ndsp && cs.pcm_bytes > 0) {
                    cs.pcm = (int16_t*)linearAlloc(cs.pcm_bytes);
                    if (!cs.pcm || fread(cs.pcm, 1, cs.pcm_bytes, bf) != cs.pcm_bytes) {
                        if (cs.pcm) linearFree(cs.pcm);
                        cs.pcm = NULL;
                        cs.clip_count = 0;
                    } else {
                        DSP_FlushDataCache(cs.pcm, cs.pcm_bytes);
                    }
                }
            }
        }
        fclose(bf);
    }

    static char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (line[0] == 'C' && line[1] == '|' && cs.color_count < MAX_CHARS) {
            color_t* c = &cs.colors[cs.color_count];
            char name[32];
            int r, g, b;
            if (sscanf(line + 2, "%23[^|]|%d|%d|%d", name, &r, &g, &b) == 4) {
                snprintf(c->name, sizeof(c->name), "%.23s", name);
                c->r = (u8)r; c->g = (u8)g; c->b = (u8)b;
                cs.color_count++;
            }
        } else if (line[0] == 'P' && line[1] == '|' && cs.panel_count < MAX_PANELS) {
            char* fld[6] = { 0 };
            int nf = 0;
            char* p = line + 2;
            while (nf < 6) {
                fld[nf++] = p;
                char* bar = strchr(p, '|');
                if (!bar) break;
                *bar = '\0';
                p = bar + 1;
            }
            if (nf < 6) continue;
            panel_t* pn = &cs.panels[cs.panel_count++];
            memset(pn, 0, sizeof(*pn));
            pn->image = atoi(fld[0]);
            snprintf(pn->character, sizeof(pn->character), "%.23s", fld[1]);
            snprintf(pn->text, sizeof(pn->text), "%.239s", fld[2]);
            pn->voice = find_clip(fld[3]);
            char* sp = fld[4];
            while (*sp && pn->sfx_n < 4) {
                char* semi = strchr(sp, ';');
                if (semi) *semi = '\0';
                int ci = find_clip(sp);
                if (ci >= 0) pn->sfx[pn->sfx_n++] = ci;
                if (!semi) break;
                sp = semi + 1;
            }
            pn->fade = strcmp(fld[5], "fade") == 0;
        } else if (line[0] == 'T' && line[1] == '|' && cs.seg_count < MAX_SEGS) {
            char name[40], bus[16];
            int st, en, rs, fi, fo;
            if (sscanf(line + 2, "%39[^|]|%15[^|]|%d|%d|%d|%d|%d", name, bus, &st, &en, &rs, &fi, &fo) == 7) {
                int ci = find_clip(name);
                if (ci < 0) continue;
                int ti = track_for_clip(ci, strcmp(bus, "Music") == 0);
                if (ti < 0) continue;
                seg_t* s = &cs.segs[cs.seg_count++];
                s->clip = ci; s->start = st; s->end = en; s->restart = rs != 0; s->fade_in = fi != 0; s->fade_out = fo != 0;
            }
        }
    }
    fclose(f);
    if (cs.panel_count == 0) {
        bb_cs_close();
        return false;
    }
    if (cs.ndsp) {
        chan_init(CH_VOICE);
        chan_init(CH_SFX0);
        chan_init(CH_SFX1);
        for (int i = 0; i < cs.track_count; i++) chan_init(CH_TRACK0 + cs.tracks[i].slot);
    }
    cs.open = true;
    enter_panel(0);
    return true;
}

void bb_cs_close(void) {
    if (cs.ndsp) {
        ndspChnWaveBufClear(CH_VOICE);
        ndspChnWaveBufClear(CH_SFX0);
        ndspChnWaveBufClear(CH_SFX1);
        for (int i = 0; i < cs.track_count; i++) ndspChnWaveBufClear(CH_TRACK0 + cs.tracks[i].slot);
    }
    if (cs.pcm) {
        linearFree(cs.pcm);
        cs.pcm = NULL;
    }
    if (cs.image) {
        defer_free(cs.image);
        cs.image = NULL;
    }
    cs.open = false;
}

bool bb_cs_active(void) { return cs.open; }
int bb_cs_panel(void) { return cs.panel; }
int bb_cs_panel_count(void) { return cs.panel_count; }

void bb_cs_next(void) {
    if (!cs.open) return;
    if (cs.panel + 1 < cs.panel_count) enter_panel(cs.panel + 1);
    else cs.finished = true;
}

/* Autoplay (a port feature; the original advances on input only): a panel moves on once its voice line has played (+0.5 s) or after a
 * reading time for its text (1.6 s + 45 ms per character), whichever is longer. The choice stays on for the following cutscenes. */
static bool g_cs_auto = false;
void bb_cs_set_autoplay(bool on) { g_cs_auto = on; }
bool bb_cs_autoplay(void) { return g_cs_auto; }

static float panel_wait_s(void) {
    const panel_t* p = &cs.panels[cs.panel];
    float w = 1.6f + 0.045f * (float)strlen(p->text);
    if (p->voice >= 0 && p->voice < cs.clip_count && cs.clips[p->voice].rate > 0) {
        const float v = (float)cs.clips[p->voice].frames / (float)cs.clips[p->voice].rate + 0.5f;
        if (v > w) w = v;
    }
    return w;
}

bool bb_cs_update(u32 kDown, float dt_s) {
    if (!cs.open) return true;
    cs.panel_t_s += dt_s > 0.05f ? 0.05f : dt_s;   /* a stalled frame (panel load) must not eat the fade-in */
    if (kDown & (KEY_B | KEY_START)) cs.finished = true;           /* ui_back: end_cutscene() */
    else if (kDown & (KEY_A | KEY_R | KEY_DRIGHT)) bb_cs_next();
    else if (g_cs_auto && cs.panel_t_s >= panel_wait_s()) bb_cs_next();
    /* global-track fades (1 s, CutsceneGlobalTracks.GLOBAL_TRACK_FADE_TIME) */
    bool changed = false;
    for (int i = 0; i < cs.track_count; i++) {
        track_t* t = &cs.tracks[i];
        if (t->vol < t->target) { t->vol += dt_s; if (t->vol > t->target) t->vol = t->target; changed = true; }
        else if (t->vol > t->target) {
            t->vol -= dt_s;
            if (t->vol <= t->target) {
                t->vol = t->target;
                if (t->target == 0.0f && !t->active && cs.ndsp) { ndspChnWaveBufClear(CH_TRACK0 + t->slot); t->started = false; }
            }
            changed = true;
        }
    }
    if (changed) apply_volumes();   /* only on fades: no per-frame DSP parameter traffic */
    if (cs.finished) {
        /* cutscene_ended: everything fades out; the console simply stops the streams */
        bb_cs_close();
        return true;
    }
    return false;
}

/* word wrap helper: fills up to max_lines lines of at most maxc characters */
void bb_cs_draw(C2D_TextBuf tb) {
    C2D_DrawRectSolid(0, 0, 0.10f, 400, 240, C2D_Color32(0, 0, 0, 0xFF));
    if (!cs.open) return;
    const panel_t* p = &cs.panels[cs.panel];
    if (cs.image) {
        C2D_Image img = C2D_SpriteSheetGetImage(cs.image, 0);
        float tw = (float)img.tex->width, th = (float)img.tex->height;
        Tex3DS_SubTexture st;
        st.width = 400; st.height = 225;
        st.left = 0.0f; st.right = 400.0f / tw;
        st.top = 1.0f; st.bottom = 1.0f - 225.0f / th;
        img.subtex = &st;
        C3D_TexSetFilter(img.tex, GPU_LINEAR, GPU_LINEAR);
        C2D_SetTintMode(C2D_TintMult);
        C2D_DrawParams dp;
        dp.pos.x = 0.0f; dp.pos.y = 7.0f; dp.pos.w = 400.0f; dp.pos.h = 225.0f;
        dp.center.x = 0.0f; dp.center.y = 0.0f; dp.depth = 0.20f; dp.angle = 0.0f;
        C2D_DrawImage(img, &dp, NULL);
    }
    if ((p->fade || cs.panel == 0) && cs.panel_t_s < 0.5f) {   /* "fade" transition: from black; the first panel always fades in (level start) */
        C2D_DrawRectSolid(0, 0, 0.90f, 400, 240, C2D_Color32(0, 0, 0, (u8)(255.0f * (1.0f - cs.panel_t_s / 0.5f))));
    }
    /* the speaker's colour (cutscene characters table), white without a speaker */
    u8 r = 0xFF, g = 0xFF, b = 0xFF;
    for (int i = 0; p->character[0] && i < cs.color_count; i++) if (strcmp(cs.colors[i].name, p->character) == 0) { r = cs.colors[i].r; g = cs.colors[i].g; b = cs.colors[i].b; }
    char lines[4][96];
    int n = 0;
    {   /* Dialogue: Noto Sans JP Regular, centred, wrapped to the box */
        const char* q = p->text;
        while (*q && n < 3) {
            while (*q == ' ') q++;
            int len = (int)strlen(q), cut = len;
            if (bb_font_width(BB_FACE_NOTO_REG, 12, q) > CS_TEXT_W) {
                int last = -1;
                for (int i = 0; i < len && i < 94; i++) {
                    char tmp[96];
                    snprintf(tmp, sizeof(tmp), "%.*s", i + 1, q);
                    if (bb_font_width(BB_FACE_NOTO_REG, 12, tmp) > CS_TEXT_W) break;
                    if (q[i] == ' ') last = i;
                }
                cut = last > 0 ? last : (len < 94 ? len : 94);
            }
            snprintf(lines[n++], sizeof(lines[0]), "%.*s", cut, q);
            q += cut;
        }
    }
    if (n > 0 || p->character[0]) {
        const float box_h = 8.0f + CS_LINE_H * (float)(n > 0 ? n : 1);
        const float box_y = 240.0f - box_h;
        const float top = p->character[0] ? box_y - CS_PLATE_H : box_y;
        /* the box, with a soft glowing top border in the speaker's colour */
        C2D_DrawRectSolid(0, top, 0.40f, 400, 240.0f - top, C2D_Color32(0, 0, 0, 0xB8));
        const u32 g0 = C2D_Color32(r, g, b, 0x00), g1 = C2D_Color32(r, g, b, 0x50);
        C2D_DrawRectangle(0, top - 8.0f, 0.41f, 400, 8.0f, g0, g0, g1, g1);
        C2D_DrawRectangle(0, top, 0.41f, 200, 1.0f, C2D_Color32(r, g, b, 0x30), C2D_Color32(r, g, b, 0xE0), C2D_Color32(r, g, b, 0x30), C2D_Color32(r, g, b, 0xE0));
        C2D_DrawRectangle(200, top, 0.41f, 200, 1.0f, C2D_Color32(r, g, b, 0xE0), C2D_Color32(r, g, b, 0x30), C2D_Color32(r, g, b, 0xE0), C2D_Color32(r, g, b, 0x30));
        /* NamePlate (cutscene_nameplate.gd): an additive band fading at both ends (GradientTexture1D at 0.156) tinted with the speaker's
         * colour, the name in Noto Sans JP Black with a grey drop shadow; hidden without a speaker */
        if (p->character[0]) {
            const u32 c0 = C2D_Color32(r, g, b, 0), c1 = C2D_Color32(r, g, b, 0x34);
            C2D_DrawRectangle(0, top + 1.0f, 0.42f, 200, CS_PLATE_H - 1.0f, c0, c1, c0, c1);
            C2D_DrawRectangle(200, top + 1.0f, 0.42f, 200, CS_PLATE_H - 1.0f, c1, c0, c1, c0);
            bb_font_draw(BB_FACE_NOTO_BLACK, 14, 200.0f, top + 1.0f, C2D_Color32((u8)(r * 0.27f), (u8)(g * 0.27f), (u8)(b * 0.27f), 0xFF), C2D_AlignCenter, p->character, 0.49f);
            bb_font_draw(BB_FACE_NOTO_BLACK, 14, 200.0f, top, C2D_Color32(r, g, b, 0xFF), C2D_AlignCenter, p->character, 0.50f);
        }
        for (int i = 0; i < n; i++) bb_font_draw(BB_FACE_NOTO_REG, 12, 200.0f, box_y + 3.0f + CS_LINE_H * (float)i, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), C2D_AlignCenter, lines[i], 0.50f);
    }
    (void)tb;
}
