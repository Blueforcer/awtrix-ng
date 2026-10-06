#include "slot_io.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "../../posix/sha256.h"

static int locate(const struct flash_device* res, struct release_slot_layout* layout) {
  uint8_t superblock[RELEASE_SLOT_SUPERBLOCK_BYTES];
  const int result = res->read(res->context, 0, superblock, sizeof superblock);
  if (result) return result;
  return release_slot_locate(superblock, res->size, res->erase_size, layout);
}

int slot_read(const struct flash_device* res, int verify, struct release_slot_layout* layout,
              struct release_slot_header* header) {
  int result = locate(res, layout);
  if (result) return result;
  uint8_t bytes[RELEASE_SLOT_HEADER_BYTES];
  if ((result = res->read(res->context, layout->header, bytes, sizeof bytes))) return result;
  if (release_slot_decode(bytes, layout->capacity, header)) return -ENOENT;
  if (!verify) return 0;
  uint8_t digest[32];
  if ((result = flash_hash_range(res, layout->image, header->image_bytes, digest))) return result;
  return memcmp(digest, header->image_sha256, sizeof digest) ? -EBADMSG : 0;
}

static int fail(struct flash_report* report, const char* stage, uint64_t offset, int error) {
  report->stage = stage;
  report->failed_offset = offset;
  report->error = error > 0 ? error : EIO;
  return -report->error;
}

struct memory {
  const uint8_t* bytes;
};

static int memory_read(void* context, uint64_t offset, void* buffer, size_t length) {
  const struct memory* memory = context;
  memcpy(buffer, memory->bytes + offset, length);
  return 0;
}

static int hash_source(const struct flash_source* image, uint8_t digest[32]) {
  enum { kChunk = 64 * 1024 };
  uint8_t* buffer = malloc(kChunk);
  if (!buffer) return -ENOMEM;
  struct sha256_state state;
  sha256_init(&state);
  int result = 0;
  for (uint64_t done = 0; done < image->length && !result;) {
    const size_t take = image->length - done < kChunk ? (size_t)(image->length - done) : kChunk;
    if (!(result = image->read(image->context, done, buffer, take))) {
      sha256_update(&state, buffer, take);
      done += take;
    }
  }
  free(buffer);
  if (!result) sha256_final(&state, digest);
  return result;
}

int slot_write(const struct flash_device* res, const struct flash_source* image,
               const struct release_slot_header* header, struct flash_report* report) {
  memset(report, 0, sizeof *report);
  report->length = image->length;
  report->stage = "done";
  struct release_slot_layout layout;
  uint8_t encoded[RELEASE_SLOT_HEADER_BYTES];
  int result = locate(res, &layout);
  if (result) return fail(report, "layout", 0, -result);
  report->offset = layout.image;
  if (release_slot_encode(header, encoded)) return fail(report, "header", layout.header, EINVAL);
  if (image->length != header->image_bytes) return fail(report, "source", 0, EINVAL);
  if (image->length > layout.capacity) return fail(report, "capacity", layout.image, EFBIG);
  uint8_t superblock[RELEASE_SLOT_SUPERBLOCK_BYTES];
  if (image->length < sizeof superblock) return fail(report, "source", 0, EINVAL);
  if ((result = image->read(image->context, 0, superblock, sizeof superblock)))
    return fail(report, "source", 0, -result);
  if (!release_slot_image_valid(superblock, image->length)) return fail(report, "source", 0, EINVAL);
  uint8_t digest[32];
  if ((result = hash_source(image, digest))) return fail(report, "source", 0, -result);
  if (memcmp(digest, header->image_sha256, sizeof digest)) return fail(report, "source", 0, EBADMSG);

  report->modified = 1;
  if ((result = res->erase(res->context, layout.header, res->erase_size)))
    return fail(report, "erase-header", layout.header, -result);
  result = flash_write_image(res, image, layout.image, report);
  report->modified = 1;
  if (result) return result;
  if (memcmp(report->sha256, header->image_sha256, sizeof digest))
    return fail(report, "verify", layout.image, EBADMSG);

  struct memory memory = {encoded};
  const struct flash_source source = {&memory, sizeof encoded, memory_read};
  struct flash_report written;
  if ((result = flash_write_image(res, &source, layout.header, &written)))
    return fail(report, "write-header", written.failed_offset, written.error);
  struct release_slot_header check;
  struct release_slot_layout again;
  if ((result = slot_read(res, 0, &again, &check)) || check.image_bytes != header->image_bytes ||
      check.counter != header->counter ||
      memcmp(check.image_sha256, header->image_sha256, sizeof digest) ||
      strcmp(check.release, header->release))
    return fail(report, "verify-header", layout.header, result ? -result : EIO);
  report->stage = "done";
  report->offset = layout.image;
  return 0;
}
