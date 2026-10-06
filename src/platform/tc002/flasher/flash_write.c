#include "flash_write.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "../../posix/sha256.h"

static int fail(struct flash_report* report, const char* stage, uint64_t offset, int error) {
  report->stage = stage;
  report->failed_offset = offset;
  report->error = error > 0 ? error : EIO;
  return -report->error;
}

static int load_block(const struct flash_source* image, uint64_t image_offset, uint8_t* block,
                      uint32_t size) {
  memset(block, 0xff, size);
  if (image_offset >= image->length) return 0;
  const uint64_t remaining = image->length - image_offset;
  const size_t take = remaining < size ? (size_t)remaining : size;
  return image->read(image->context, image_offset, block, take);
}

static int check_request(const struct flash_device* device, uint64_t offset, uint64_t length,
                         struct flash_report* report) {
  if (!device->erase_size || device->erase_size > FLASH_MAX_ERASE_SIZE ||
      device->size % device->erase_size)
    return fail(report, "geometry", 0, EINVAL);
  if (offset % device->erase_size) return fail(report, "offset", offset, EINVAL);
  if (!length) return fail(report, "length", offset, EINVAL);
  if (offset > device->size || length > device->size - offset)
    return fail(report, "size", offset, EFBIG);
  return 0;
}

int flash_write_image(const struct flash_device* device, const struct flash_source* image,
                      uint64_t offset, struct flash_report* report) {
  memset(report, 0, sizeof *report);
  report->offset = offset;
  report->length = image->length;
  report->stage = "done";
  int result = check_request(device, offset, image->length, report);
  if (result) return result;
  const uint32_t erase = device->erase_size;
  report->span = (image->length + erase - 1) / erase * erase;
  report->blocks = (uint32_t)(report->span / erase);
  uint8_t* want = malloc(erase);
  uint8_t* have = malloc(erase);
  if (!want || !have) {
    free(want);
    free(have);
    return fail(report, "memory", offset, ENOMEM);
  }
  for (uint64_t done = 0; done < report->span; done += erase) {
    const uint64_t at = offset + done;
    if ((result = load_block(image, done, want, erase))) {
      result = fail(report, "image-read", at, -result);
      goto out;
    }
    if ((result = device->read(device->context, at, have, erase))) {
      result = fail(report, "read", at, -result);
      goto out;
    }
    if (!memcmp(want, have, erase)) {
      ++report->skipped;
      continue;
    }
    for (int attempt = 0;; ++attempt) {
      report->modified = 1;
      if ((result = device->erase(device->context, at, erase))) {
        result = fail(report, "erase", at, -result);
        goto out;
      }
      if ((result = device->write(device->context, at, want, erase))) {
        result = fail(report, "write", at, -result);
        goto out;
      }
      if ((result = device->read(device->context, at, have, erase))) {
        result = fail(report, "verify-read", at, -result);
        goto out;
      }
      if (!memcmp(want, have, erase)) break;
      if (attempt + 1 >= FLASH_WRITE_ATTEMPTS) {
        result = fail(report, "verify", at, EIO);
        goto out;
      }
      ++report->retries;
    }
    ++report->written;
  }
  struct sha256_state hash;
  sha256_init(&hash);
  for (uint64_t done = 0; done < report->span; done += erase) {
    const uint64_t at = offset + done;
    if ((result = load_block(image, done, want, erase))) {
      result = fail(report, "image-read", at, -result);
      goto out;
    }
    if ((result = device->read(device->context, at, have, erase))) {
      result = fail(report, "final-read", at, -result);
      goto out;
    }
    if (memcmp(want, have, erase)) {
      result = fail(report, "final-verify", at, EIO);
      goto out;
    }
    const uint64_t remaining = image->length - done;
    sha256_update(&hash, have, remaining < erase ? (size_t)remaining : erase);
  }
  sha256_final(&hash, report->sha256);
  result = 0;
out:
  free(want);
  free(have);
  return result;
}

int flash_hash_range(const struct flash_device* device, uint64_t offset, uint64_t length,
                     uint8_t digest[32]) {
  if (offset > device->size || length > device->size - offset) return -EFBIG;
  enum { kChunk = 64 * 1024 };
  uint8_t* buffer = malloc(kChunk);
  if (!buffer) return -ENOMEM;
  struct sha256_state hash;
  sha256_init(&hash);
  int result = 0;
  for (uint64_t done = 0; done < length;) {
    const size_t take = length - done < kChunk ? (size_t)(length - done) : kChunk;
    if ((result = device->read(device->context, offset + done, buffer, take))) break;
    sha256_update(&hash, buffer, take);
    done += take;
  }
  free(buffer);
  if (!result) sha256_final(&hash, digest);
  return result;
}
