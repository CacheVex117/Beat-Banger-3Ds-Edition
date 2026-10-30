/**
 * @file cutscene.h
 * @brief Pre/post-level cutscenes (tools/convert_cutscene.py output in romfs:/stages/<id>/cutscene/).
 *
 * Behaviour follows scenes/cutscene.gd + cutscene_tracks_panel.gd + class/cutscene.gd: a click/A advances one panel, B/START skips
 * the whole scene, every panel change stops the previous panel's sounds and plays the new panel's voice line and SFX, and
 * global tracks (ambience/music) play over their segments of panels, optionally fading in/out.
 * The panel image fills the top screen; the dialogue and the character name plate are drawn over it.
 */
#ifndef BB_CUTSCENE_H
#define BB_CUTSCENE_H

#include <stdbool.h>
#include <stdint.h>
#include <3ds.h>
#include <citro2d.h>

/* stage_dir like "romfs:/stages/cathy_e1", which = "pre" or "post". False if the stage has no such cutscene. */
bool bb_cs_open(const char* stage_dir, const char* which, bool ndsp_available);
void bb_cs_close(void);
bool bb_cs_active(void);
/* kDown = keys pressed this frame, dt_s = frame time. Returns true once the scene is over (last panel passed or skipped). */
bool bb_cs_update(u32 kDown, float dt_s);
void bb_cs_draw(C2D_TextBuf tb);
/* Volumes 0..1: master is applied on top of the bus volumes. */
void bb_cs_set_volumes(float master, float music, float sfx, float voice);
int  bb_cs_panel(void);
int  bb_cs_panel_count(void);
void bb_cs_flush(void);           /* call right after C3D_FrameBegin: frees panel textures the GPU is done with */
void bb_cs_next(void);            /* advance one panel (also what A does) */
/* Autoplay: panels advance by themselves after their voice line / a reading time (port feature, persists across cutscenes). */
void bb_cs_set_autoplay(bool on);
bool bb_cs_autoplay(void);

#endif
