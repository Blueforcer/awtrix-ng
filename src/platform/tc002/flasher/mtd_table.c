#include "mtd_table.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int parse_line(const char* line, const char* end, struct mtd_entry* entry) {
  if (end - line < 4 || strncmp(line, "mtd", 3)) return -EINVAL;
  char* cursor;
  const unsigned long index = strtoul(line + 3, &cursor, 10);
  if (cursor == line + 3 || cursor >= end || *cursor != ':') return -EINVAL;
  const char* field = cursor + 1;
  const unsigned long long size = strtoull(field, &cursor, 16);
  if (cursor == field || cursor >= end) return -EINVAL;
  field = cursor;
  const unsigned long long erase = strtoull(field, &cursor, 16);
  if (cursor == field || cursor >= end || erase == 0 || erase > UINT32_MAX) return -EINVAL;
  while (cursor < end && *cursor == ' ') ++cursor;
  if (cursor >= end || *cursor != '"') return -EINVAL;
  const char* name = cursor + 1;
  const char* quote = memchr(name, '"', (size_t)(end - name));
  if (!quote || (size_t)(quote - name) >= sizeof entry->name) return -EINVAL;
  entry->index = (unsigned)index;
  entry->size = size;
  entry->erase_size = (uint32_t)erase;
  memcpy(entry->name, name, (size_t)(quote - name));
  entry->name[quote - name] = '\0';
  return 0;
}

static int find(const char* text, int by_name, unsigned index, const char* name,
                struct mtd_entry* entry) {
  int found = 0;
  for (const char* line = text; *line;) {
    const char* end = strchr(line, '\n');
    if (!end) end = line + strlen(line);
    struct mtd_entry candidate;
    if (!parse_line(line, end, &candidate) &&
        (by_name ? !strcmp(candidate.name, name) : candidate.index == index)) {
      if (found) return -EINVAL;
      *entry = candidate;
      found = 1;
    }
    line = *end ? end + 1 : end;
  }
  return found ? 0 : -ENOENT;
}

int mtd_table_find(const char* text, unsigned index, struct mtd_entry* entry) {
  return find(text, 0, index, NULL, entry);
}

int mtd_table_find_name(const char* text, const char* name, struct mtd_entry* entry) {
  return find(text, 1, 0, name, entry);
}
