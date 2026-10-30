/**
 * @file save_io.c
 * @brief SD-card persistence for settings + progression (core/save_data.c byte image).
 *
 * Writes are crash-safe: the new image goes to save.tmp and is read back and validated, then the old save.bin is
 * kept as save.bak while save.tmp is renamed into place. Loading tries save.bin, then save.bak, then defaults.
 */
#include "save_io.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define SAVE_DIR  "sdmc:/3ds/beatbanger"
#define SAVE_BIN  SAVE_DIR "/save.bin"
#define SAVE_BAK  SAVE_DIR "/save.bak"
#define SAVE_TMP  SAVE_DIR "/save.tmp"

static bool read_file(const char* path, uint8_t* buf, size_t cap, size_t* out_n) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    size_t n = fread(buf, 1, cap, f);
    bool at_end = (fgetc(f) == EOF);
    fclose(f);
    *out_n = n;
    return at_end; /* a file larger than cap is invalid */
}

bool bb_save_io_load(bb_save_t* s) {
    uint8_t buf[BB_SAVE_MAX_BYTES];
    size_t n = 0;
    const char* paths[2] = { SAVE_BIN, SAVE_BAK };
    for (int i = 0; i < 2; i++) {
        if (read_file(paths[i], buf, sizeof(buf), &n) && bb_save_deserialize(s, buf, n)) {
            return true;
        }
    }
    bb_save_defaults(s);
    return false;
}

bool bb_save_io_store(const bb_save_t* s) {
    uint8_t buf[BB_SAVE_MAX_BYTES], check[BB_SAVE_MAX_BYTES];
    size_t n = bb_save_serialize(s, buf, sizeof(buf));
    if (n == 0) {
        return false;
    }
    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);

    FILE* f = fopen(SAVE_TMP, "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(buf, 1, n, f) == n;
    ok = (fflush(f) == 0) && ok;
    fclose(f);
    if (!ok) {
        remove(SAVE_TMP);
        return false;
    }
    /* read back and validate before replacing the good copy */
    size_t m = 0;
    bb_save_t verify;
    if (!read_file(SAVE_TMP, check, sizeof(check), &m) || m != n || memcmp(buf, check, n) != 0 || !bb_save_deserialize(&verify, check, m)) {
        remove(SAVE_TMP);
        return false;
    }
    remove(SAVE_BAK);
    rename(SAVE_BIN, SAVE_BAK);   /* may fail on first save: fine */
    if (rename(SAVE_TMP, SAVE_BIN) != 0) {
        rename(SAVE_BAK, SAVE_BIN); /* put the previous copy back */
        remove(SAVE_TMP);
        return false;
    }
    return true;
}
