#pragma once

#include <stddef.h>
#include <stdint.h>

#include "flash_write.h"
#include "mtd_table.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mtd_handle {
  int fd;
  int nor;
  int writeable;
  uint32_t write_size;
  struct mtd_entry entry;
  struct flash_device device;
};

int mtd_open(const char* path, int for_write, struct mtd_handle* handle, char* error,
             size_t error_size);
void mtd_close(struct mtd_handle* handle);

/* The character device of the partition called name: <dir>/mtd<N>, or mtd<N>ro for reading. */
int mtd_device_path(const char* dir, const char* name, int for_write, char* path, size_t size,
                    unsigned* index);

int fd_read_exact(int fd, uint64_t offset, void* buffer, size_t length);
int fd_write_exact(int fd, uint64_t offset, const void* buffer, size_t length);

#ifdef __cplusplus
}
#endif
