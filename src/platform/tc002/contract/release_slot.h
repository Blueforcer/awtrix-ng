#pragma once

#include <stddef.h>
#include <stdint.h>

#include "release_name.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The release slot: the squashfs image of the installed release in the res partition, behind
 * the squashfs res itself starts with. Its first erase block holds the header; the image starts
 * at the next erase block and may run to the end of the partition. A writer erases the header
 * first and writes it last, once the image reads back correctly, so an interrupted write leaves
 * no slot. The loader verifies the header and the image and mounts the image through loop.ko;
 * awtrix-tc002-flash writes it. */
#define RELEASE_SLOT_MAGIC "AWSLOT01"
#define RELEASE_SLOT_HEADER_BYTES 154
#define RELEASE_SLOT_SUPERBLOCK_BYTES 96

struct release_slot_layout {
  uint64_t header;
  uint64_t image;
  uint64_t capacity;
};

struct release_slot_header {
  uint64_t image_bytes;
  uint64_t counter;
  uint8_t image_sha256[32];
  char release[TC002_RELEASE_NAME_LIMIT + 1];
};

/* The layout behind the squashfs whose superblock starts a partition of this geometry: -EINVAL
 * when the partition does not start with a squashfs, -ENOSPC when no image block is left. */
int release_slot_locate(const uint8_t superblock[RELEASE_SLOT_SUPERBLOCK_BYTES],
                        uint64_t partition_size, uint32_t erase_size,
                        struct release_slot_layout* layout);

/* Whether image_bytes starting with superblock are a slot image: a squashfs of that size once
 * mksquashfs padded it to 4 KiB. */
int release_slot_image_valid(const uint8_t superblock[RELEASE_SLOT_SUPERBLOCK_BYTES],
                             uint64_t image_bytes);

/* -EINVAL for an empty image, a counter of 0 or above 2^63 - 1, or an invalid release name. */
int release_slot_encode(const struct release_slot_header* header,
                        uint8_t out[RELEASE_SLOT_HEADER_BYTES]);

/* -EINVAL unless the magic, the checksum and every field are valid and the image fits the
 * capacity; an erased header block is never valid. */
int release_slot_decode(const uint8_t in[RELEASE_SLOT_HEADER_BYTES], uint64_t capacity,
                        struct release_slot_header* header);

#ifdef __cplusplus
}
#endif
