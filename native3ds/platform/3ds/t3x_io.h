/**
 * @file t3x_io.h
 * @brief Texture-page loading that shares the SD card politely with the song stream.
 *
 * The FS service runs one request at a time and the game thread streams the song through it. C2D_SpriteSheetLoad's
 * stdio path makes hundreds of tiny requests per page (console: audio underruns); one whole-file read makes the song
 * wait for the entire page (console: 128-146 ms audio steps). Reading in 32 KiB pieces and decoding from memory keeps
 * each wait to a few ms (measured on the New 2DS XL with the Tracer mod, video.c).
 */
#ifndef BB_T3X_IO_H
#define BB_T3X_IO_H

#include <stdint.h>
#include <citro2d.h>

/* Load a .t3x sprite sheet; read_ms (optional) receives the time spent reading the file. NULL on failure. */
C2D_SpriteSheet bb_t3x_load(const char* path, uint32_t* read_ms);

#endif /* BB_T3X_IO_H */
