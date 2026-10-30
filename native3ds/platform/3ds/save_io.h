/**
 * @file save_io.h
 * @brief Crash-safe SD persistence for core/save_data (see save_io.c).
 */
#ifndef BB_SAVE_IO_H
#define BB_SAVE_IO_H

#include "save_data.h"

/* Load save.bin (else save.bak, else defaults). Returns true if a valid file was read. */
bool bb_save_io_load(bb_save_t* s);
/* Write atomically (tmp + validate + rotate). Returns true on success. */
bool bb_save_io_store(const bb_save_t* s);

#endif
