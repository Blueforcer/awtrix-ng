#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mtd_entry {
  unsigned index;
  uint64_t size;
  uint32_t erase_size;
  char name[64];
};

/* The entry of mtd<index>, or of the partition called name; 0, -ENOENT, or -EINVAL when the
 * table lists it twice. */
int mtd_table_find(const char* text, unsigned index, struct mtd_entry* entry);
int mtd_table_find_name(const char* text, const char* name, struct mtd_entry* entry);

#ifdef __cplusplus
}
#endif
