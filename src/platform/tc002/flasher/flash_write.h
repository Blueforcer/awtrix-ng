#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLASH_MAX_ERASE_SIZE (256u * 1024u)
#define FLASH_WRITE_ATTEMPTS 3

struct flash_device {
  void* context;
  uint64_t size;
  uint32_t erase_size;
  int (*erase)(void* context, uint64_t offset, uint32_t length);
  int (*read)(void* context, uint64_t offset, void* buffer, size_t length);
  int (*write)(void* context, uint64_t offset, const void* buffer, size_t length);
};

struct flash_source {
  void* context;
  uint64_t length;
  int (*read)(void* context, uint64_t offset, void* buffer, size_t length);
};

struct flash_report {
  uint64_t offset;
  uint64_t length;
  uint64_t span;
  uint32_t blocks;
  uint32_t skipped;
  uint32_t written;
  uint32_t retries;
  int modified;
  const char* stage;
  uint64_t failed_offset;
  int error;
  uint8_t sha256[32];
};

int flash_write_image(const struct flash_device* device, const struct flash_source* image,
                      uint64_t offset, struct flash_report* report);

int flash_hash_range(const struct flash_device* device, uint64_t offset, uint64_t length,
                     uint8_t digest[32]);

#ifdef __cplusplus
}
#endif
