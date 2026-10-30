/**
 * @file t3x_io.c
 * @brief Chunked .t3x loading (see t3x_io.h).
 */
#include "t3x_io.h"
#include <stdio.h>
#include <stdlib.h>

#define PAGE_READ_CHUNK (32 * 1024)

C2D_SpriteSheet bb_t3x_load(const char* path, uint32_t* read_ms) {
    uint64_t t0 = osGetTime();
    C2D_SpriteSheet sh = NULL;
    FILE* f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        void* buf = size > 0 ? malloc((size_t)size) : NULL;
        setvbuf(f, NULL, _IONBF, 0);                 /* straight into buf, no stdio copy */
        size_t got = 0;
        while (buf && got < (size_t)size) {
            size_t want = (size_t)size - got < PAGE_READ_CHUNK ? (size_t)size - got : PAGE_READ_CHUNK;
            size_t n = fread((char*)buf + got, 1, want, f);
            if (n == 0) break;
            got += n;
        }
        if (read_ms) *read_ms = (uint32_t)(osGetTime() - t0);
        if (buf && got == (size_t)size) {
            sh = C2D_SpriteSheetLoadFromMem(buf, (size_t)size);
        }
        free(buf);
        fclose(f);
    }
    return sh;
}
