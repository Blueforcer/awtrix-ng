#include "release_slot.h"

#include <errno.h>
#include <string.h>

#include "../../posix/sha256.h"

#define SQUASHFS_MAGIC 0x73717368u
#define SQUASHFS_PADDING 4096u
#define COUNTER_LIMIT 0x7fffffffffffffffull

enum {
  kImageBytes = 8,
  kCounter = 16,
  kImageSha256 = 24,
  kReleaseLength = 56,
  kRelease = 58,
  kHeaderSha256 = kRelease + TC002_RELEASE_NAME_LIMIT,
};

_Static_assert(kHeaderSha256 + 32 == RELEASE_SLOT_HEADER_BYTES, "header layout");

static uint64_t little_endian(const uint8_t* bytes, int count) {
  uint64_t value = 0;
  for (int i = count - 1; i >= 0; --i) value = value << 8 | bytes[i];
  return value;
}

static uint64_t big_endian(const uint8_t* bytes, int count) {
  uint64_t value = 0;
  for (int i = 0; i < count; ++i) value = value << 8 | bytes[i];
  return value;
}

static void put_big_endian(uint8_t* bytes, uint64_t value, int count) {
  for (int i = count - 1; i >= 0; --i) {
    bytes[i] = (uint8_t)value;
    value >>= 8;
  }
}

static void checksum(const uint8_t* header, uint8_t digest[32]) {
  struct sha256_state state;
  sha256_init(&state);
  sha256_update(&state, header, kHeaderSha256);
  sha256_final(&state, digest);
}

static uint64_t squashfs_bytes(const uint8_t* superblock) {
  if (little_endian(superblock, 4) != SQUASHFS_MAGIC) return 0;
  const uint64_t used = little_endian(superblock + 40, 8);
  return used < RELEASE_SLOT_SUPERBLOCK_BYTES ? 0 : used;
}

int release_slot_image_valid(const uint8_t superblock[RELEASE_SLOT_SUPERBLOCK_BYTES],
                             uint64_t image_bytes) {
  const uint64_t used = squashfs_bytes(superblock);
  return used && used <= image_bytes &&
         image_bytes <= (used + SQUASHFS_PADDING - 1) / SQUASHFS_PADDING * SQUASHFS_PADDING;
}

int release_slot_locate(const uint8_t superblock[RELEASE_SLOT_SUPERBLOCK_BYTES],
                        uint64_t partition_size, uint32_t erase_size,
                        struct release_slot_layout* layout) {
  if (!erase_size || partition_size % erase_size || partition_size < 2ull * erase_size)
    return -EINVAL;
  const uint64_t used = squashfs_bytes(superblock);
  if (!used || used > partition_size) return -EINVAL;
  const uint64_t header = (used + erase_size - 1) / erase_size * erase_size;
  if (header > partition_size - 2ull * erase_size) return -ENOSPC;
  layout->header = header;
  layout->image = header + erase_size;
  layout->capacity = partition_size - layout->image;
  return 0;
}

int release_slot_encode(const struct release_slot_header* header,
                        uint8_t out[RELEASE_SLOT_HEADER_BYTES]) {
  size_t length = 0;
  while (length < sizeof header->release && header->release[length]) ++length;
  if (!header->image_bytes || !header->counter || header->counter > COUNTER_LIMIT ||
      !tc002_release_name_valid(header->release, length))
    return -EINVAL;
  memset(out, 0, RELEASE_SLOT_HEADER_BYTES);
  memcpy(out, RELEASE_SLOT_MAGIC, 8);
  put_big_endian(out + kImageBytes, header->image_bytes, 8);
  put_big_endian(out + kCounter, header->counter, 8);
  memcpy(out + kImageSha256, header->image_sha256, 32);
  put_big_endian(out + kReleaseLength, length, 2);
  memcpy(out + kRelease, header->release, length);
  checksum(out, out + kHeaderSha256);
  return 0;
}

int release_slot_decode(const uint8_t in[RELEASE_SLOT_HEADER_BYTES], uint64_t capacity,
                        struct release_slot_header* header) {
  uint8_t digest[32];
  if (memcmp(in, RELEASE_SLOT_MAGIC, 8)) return -EINVAL;
  checksum(in, digest);
  if (memcmp(digest, in + kHeaderSha256, 32)) return -EINVAL;
  const uint64_t image_bytes = big_endian(in + kImageBytes, 8);
  const uint64_t counter = big_endian(in + kCounter, 8);
  const size_t length = (size_t)big_endian(in + kReleaseLength, 2);
  if (!image_bytes || image_bytes > capacity || !counter || counter > COUNTER_LIMIT ||
      !tc002_release_name_valid((const char*)in + kRelease, length))
    return -EINVAL;
  for (size_t i = length; i < TC002_RELEASE_NAME_LIMIT; ++i)
    if (in[kRelease + i]) return -EINVAL;
  memset(header, 0, sizeof *header);
  header->image_bytes = image_bytes;
  header->counter = counter;
  memcpy(header->image_sha256, in + kImageSha256, 32);
  memcpy(header->release, in + kRelease, length);
  return 0;
}
