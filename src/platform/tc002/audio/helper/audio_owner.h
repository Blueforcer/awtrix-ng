#ifndef AWTRIX_TC002_AUDIO_OWNER_H
#define AWTRIX_TC002_AUDIO_OWNER_H

#include <stddef.h>
#include <stdint.h>

int ah_numeric(const char *text);
/* True when the whole file fit into out. */
int ah_read_small(const char *path, uint8_t *out, size_t capacity, size_t *length);
/* True when no task under proc is the stock GUI, by name or by executable. */
int ah_stock_gui_absent(const char *proc);
/* 1 when path contains text, 0 when it does not, -1 when path cannot be read. */
int ah_file_contains(const char *path, const char *text);

#endif
