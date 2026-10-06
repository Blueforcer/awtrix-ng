#ifndef AWTRIX_TC002_RELEASE_NAME_H
#define AWTRIX_TC002_RELEASE_NAME_H

#include <stddef.h>

/* Release names: the release of manifests, update packages, the release slot and the supervisor
 * protocol. ReleaseName.h gives C++ the same rule; tools/tc002/install/bundle.py NAME spells it. */
#define TC002_RELEASE_NAME_LIMIT 64

/* 1 to 64 characters from [A-Za-z0-9._+-], starting with a letter or a digit. Paths inside a
 * release are made of such components too. */
static inline int tc002_release_component_valid(const char* name, size_t size) {
  if (!size || size > TC002_RELEASE_NAME_LIMIT) return 0;
  for (size_t i = 0; i < size; ++i) {
    const char c = name[i];
    const int alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (!alnum && (i == 0 || (c != '.' && c != '_' && c != '+' && c != '-'))) return 0;
  }
  return 1;
}

/* A component that does not end in ".partial", the suffix of a release still being written. */
static inline int tc002_release_name_valid(const char* name, size_t size) {
  static const char partial[] = ".partial";
  const size_t suffix = sizeof partial - 1;
  if (!tc002_release_component_valid(name, size)) return 0;
  if (size < suffix) return 1;
  for (size_t i = 0; i < suffix; ++i)
    if (name[size - suffix + i] != partial[i]) return 1;
  return 0;
}

#endif
