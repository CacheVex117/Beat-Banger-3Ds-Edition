/**
 * @file font.h
 * @brief Bitmap UI font (romfs:/ui/font.t3x + font.txt, tools/build_font.py): the original game's Encode Sans SC Condensed
 *        (body / bold) and Anton (head), pre-rendered at the pixel sizes the menus use and drawn 1:1.
 */
#ifndef BB_FONT_H
#define BB_FONT_H

#include <stdbool.h>
#include <3ds.h>
#include <citro2d.h>

/* BB_FACE_NOTO: Noto Sans JP Black with a baked black outline (gameplay HUD: judgements, combo, points); HUD characters only. */
/* BB_FACE_NOTO_BLACK / BB_FACE_NOTO_REG: plain Noto Sans JP Black / Regular (cutscene name plate and dialogue). */
typedef enum { BB_FACE_BODY = 0, BB_FACE_BOLD, BB_FACE_HEAD, BB_FACE_HAND, BB_FACE_NOTO, BB_FACE_NOTO_BLACK, BB_FACE_NOTO_REG } bb_face_t;

bool  bb_font_init(void);               /* false = atlas missing; callers fall back to the system font */
void  bb_font_shutdown(void);
bool  bb_font_ready(void);

/* Map a legacy system-font `scale` (1.0 ~ 26 px) onto the nearest face/size. */
void  bb_font_from_scale(float scale, bb_face_t* face, int* size);
float bb_font_width(bb_face_t face, int size, const char* s);
int   bb_font_line_height(bb_face_t face, int size);
/* y = top of the line box. align = C2D_AlignLeft / C2D_AlignCenter / C2D_AlignRight. Shadow is added for sizes >= 17. */
void  bb_font_draw(bb_face_t face, int size, float x, float y, u32 color, u32 align, const char* s, float z);

/* Small caps like the original menus: the first letter of every word at `size`, the rest upper-cased at ~80 % of it. */
void  bb_font_draw_smallcaps(bb_face_t face, int size, float x, float y, u32 color, u32 align, const char* s, float z);
float bb_font_width_smallcaps(bb_face_t face, int size, const char* s);
/* Text centred on (cx, cy) and rotated by `angle` radians (positive = clockwise on screen); no shadow pass. */
void  bb_font_draw_rot(bb_face_t face, int size, float cx, float cy, float angle, u32 color, const char* s, float z);
/* Same, with a uniform scale (grade stamp, pop-ins); scales up from the pre-rendered size, so keep it moderate. */
void  bb_font_draw_xf(bb_face_t face, int size, float cx, float cy, float scale, float angle, u32 color, const char* s, float z);

#endif
