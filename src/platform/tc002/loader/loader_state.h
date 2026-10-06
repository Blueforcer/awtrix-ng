#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int loader_read_attempts(const char* path);
int loader_store_attempts(const char* path, int attempts);
int loader_clear_attempts(const char* path);
int loader_append_log(const char* path, const char* text, size_t limit, int durable);
int loader_ensure_directory(const char* path, int mode);

#ifdef __cplusplus
}
#endif
