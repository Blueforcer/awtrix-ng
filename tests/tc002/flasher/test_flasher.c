#include "../../support.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/tc002/contract/deploy_token.h"
#include "platform/tc002/flasher/flash_write.h"
#include "platform/tc002/flasher/mtd_table.h"
#include "platform/tc002/flasher/slot_io.h"
#include "platform/posix/sha256.h"



#define CHECK(condition) AWTRIX_TEST_CHECK(condition, "flasher")

enum { kErase = 4096, kBlocks = 16, kSize = kErase * kBlocks };

struct fake_nor {
  uint8_t cells[kSize];
  unsigned erases;
  unsigned writes;
  int erase_is_noop;
  int fail_erase_at;
  unsigned stuck_offset;
  uint8_t stuck_mask;
};

static int nor_erase(void* context, uint64_t offset, uint32_t length) {
  struct fake_nor* nor = context;
  if (offset % kErase || length != kErase || offset + length > kSize) return -EINVAL;
  ++nor->erases;
  if (nor->fail_erase_at >= 0 && (uint64_t)nor->fail_erase_at == offset) return -EIO;
  if (!nor->erase_is_noop) memset(nor->cells + offset, 0xff, length);
  return 0;
}

static int nor_read(void* context, uint64_t offset, void* buffer, size_t length) {
  struct fake_nor* nor = context;
  if (offset + length > kSize) return -EIO;
  memcpy(buffer, nor->cells + offset, length);
  return 0;
}

static int nor_write(void* context, uint64_t offset, const void* buffer, size_t length) {
  struct fake_nor* nor = context;
  if (offset + length > kSize) return -EIO;
  const uint8_t* bytes = buffer;
  ++nor->writes;
  for (size_t i = 0; i < length; ++i) {
    uint8_t value = bytes[i];
    if (offset + i == nor->stuck_offset) value &= (uint8_t)~nor->stuck_mask;
    nor->cells[offset + i] &= value;
  }
  return 0;
}

static struct fake_nor* nor_new(uint8_t fill) {
  struct fake_nor* nor = calloc(1, sizeof *nor);
  memset(nor->cells, fill, sizeof nor->cells);
  nor->fail_erase_at = -1;
  nor->stuck_offset = (unsigned)-1;
  return nor;
}

static struct flash_device nor_device(struct fake_nor* nor) {
  struct flash_device device = {.context = nor, .size = kSize, .erase_size = kErase,
                                .erase = nor_erase, .read = nor_read, .write = nor_write};
  return device;
}

struct memory_image {
  const uint8_t* bytes;
};

static int image_read(void* context, uint64_t offset, void* buffer, size_t length) {
  const struct memory_image* image = context;
  memcpy(buffer, image->bytes + offset, length);
  return 0;
}

static void pattern(uint8_t* bytes, size_t length, unsigned seed) {
  for (size_t i = 0; i < length; ++i) bytes[i] = (uint8_t)((i * 131u + seed * 7u) ^ (i >> 7));
}

static void digest_of(const uint8_t* bytes, size_t length, uint8_t digest[32]) {
  struct sha256_state state;
  sha256_init(&state);
  sha256_update(&state, bytes, length);
  sha256_final(&state, digest);
}

static void test_sha256_vectors(void) {
  struct {
    const char* input;
    const char* hex;
  } vectors[] = {
      {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
      {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
       "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
  };
  for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; ++i) {
    uint8_t digest[32];
    char hex[65];
    digest_of((const uint8_t*)vectors[i].input, strlen(vectors[i].input), digest);
    sha256_hex(digest, hex);
    CHECK(!strcmp(hex, vectors[i].hex));
  }
  struct sha256_state state;
  uint8_t digest[32];
  char hex[65];
  uint8_t chunk[1001];
  memset(chunk, 'a', sizeof chunk);
  sha256_init(&state);
  for (int i = 0; i < 1000; ++i) sha256_update(&state, chunk, i % 2 ? 999 : 1001);
  sha256_final(&state, digest);
  sha256_hex(digest, hex);
  CHECK(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

static void test_blank_device_written_and_tail_padded(void) {
  struct fake_nor* nor = nor_new(0xff);
  const size_t length = kErase * 3 + 100;
  uint8_t* bytes = malloc(length);
  pattern(bytes, length, 1);
  struct memory_image memory = {bytes};
  struct flash_source image = {.context = &memory, .length = length, .read = image_read};
  struct flash_device device = nor_device(nor);
  memset(nor->cells + kErase * 3, 0x00, kErase * 2);
  struct flash_report report;
  CHECK(flash_write_image(&device, &image, 0, &report) == 0);
  CHECK(report.blocks == 4 && report.span == kErase * 4);
  CHECK(report.written == 4 && report.skipped == 0 && report.modified);
  CHECK(!memcmp(nor->cells, bytes, length));
  for (size_t i = length; i < kErase * 4; ++i) CHECK(nor->cells[i] == 0xff);
  for (size_t i = kErase * 4; i < kErase * 5; ++i) CHECK(nor->cells[i] == 0x00);
  uint8_t expected[32];
  digest_of(bytes, length, expected);
  CHECK(!memcmp(report.sha256, expected, 32));
  uint8_t hashed[32];
  CHECK(flash_hash_range(&device, 0, length, hashed) == 0 && !memcmp(hashed, expected, 32));
  free(bytes);
  free(nor);
}

static void test_identical_blocks_are_skipped(void) {
  struct fake_nor* nor = nor_new(0xff);
  const size_t length = kErase * 5;
  uint8_t* bytes = malloc(length);
  pattern(bytes, length, 2);
  memcpy(nor->cells, bytes, length);
  nor->cells[kErase * 2 + 17] ^= 0x40;
  struct memory_image memory = {bytes};
  struct flash_source image = {.context = &memory, .length = length, .read = image_read};
  struct flash_device device = nor_device(nor);
  struct flash_report report;
  CHECK(flash_write_image(&device, &image, 0, &report) == 0);
  CHECK(report.written == 1 && report.skipped == 4 && nor->erases == 1);
  CHECK(!memcmp(nor->cells, bytes, length));
  nor->erases = 0;
  CHECK(flash_write_image(&device, &image, 0, &report) == 0);
  CHECK(report.written == 0 && report.skipped == 5 && !report.modified && nor->erases == 0);
  free(bytes);
  free(nor);
}

static void test_chunked_offsets_compose(void) {
  struct fake_nor* nor = nor_new(0x5a);
  const size_t length = kErase * 7 + 3;
  uint8_t* bytes = malloc(length);
  pattern(bytes, length, 3);
  struct flash_device device = nor_device(nor);
  const size_t chunk = kErase * 2;
  for (size_t at = 0; at < length; at += chunk) {
    struct memory_image memory = {bytes + at};
    const size_t take = length - at < chunk ? length - at : chunk;
    struct flash_source image = {.context = &memory, .length = take, .read = image_read};
    struct flash_report report;
    CHECK(flash_write_image(&device, &image, at, &report) == 0);
  }
  CHECK(!memcmp(nor->cells, bytes, length));
  for (size_t i = length; i < kErase * 8; ++i) CHECK(nor->cells[i] == 0xff);
  CHECK(nor->cells[kErase * 8] == 0x5a);
  free(bytes);
  free(nor);
}

static void test_requests_rejected_before_modification(void) {
  struct fake_nor* nor = nor_new(0x11);
  uint8_t* bytes = calloc(1, kSize + 1);
  struct memory_image memory = {bytes};
  struct flash_device device = nor_device(nor);
  struct flash_report report;
  struct flash_source too_big = {.context = &memory, .length = kSize + 1, .read = image_read};
  CHECK(flash_write_image(&device, &too_big, 0, &report) == -EFBIG && !report.modified);
  struct flash_source small = {.context = &memory, .length = 10, .read = image_read};
  CHECK(flash_write_image(&device, &small, 100, &report) == -EINVAL && !report.modified);
  CHECK(!strcmp(report.stage, "offset"));
  CHECK(flash_write_image(&device, &small, kSize - kErase + kErase, &report) == -EFBIG);
  struct flash_source past_end = {.context = &memory, .length = kErase + 1, .read = image_read};
  CHECK(flash_write_image(&device, &past_end, kSize - kErase, &report) == -EFBIG);
  struct flash_source empty = {.context = &memory, .length = 0, .read = image_read};
  CHECK(flash_write_image(&device, &empty, 0, &report) == -EINVAL);
  CHECK(nor->erases == 0 && nor->writes == 0);
  for (size_t i = 0; i < kSize; ++i) CHECK(nor->cells[i] == 0x11);
  free(bytes);
  free(nor);
}

static void test_faults_are_reported(void) {
  const size_t length = kErase * 2;
  uint8_t* bytes = malloc(length);
  pattern(bytes, length, 4);
  struct memory_image memory = {bytes};
  struct flash_source image = {.context = &memory, .length = length, .read = image_read};
  struct flash_report report;

  struct fake_nor* stuck = nor_new(0x00);
  stuck->stuck_offset = kErase + 5;
  stuck->stuck_mask = 0x01;
  bytes[kErase + 5] |= 0x01;
  struct flash_device device = nor_device(stuck);
  CHECK(flash_write_image(&device, &image, 0, &report) == -EIO);
  CHECK(!strcmp(report.stage, "verify") && report.failed_offset == kErase && report.modified);
  CHECK(report.retries == FLASH_WRITE_ATTEMPTS - 1);

  struct fake_nor* no_erase = nor_new(0x00);
  no_erase->erase_is_noop = 1;
  device = nor_device(no_erase);
  CHECK(flash_write_image(&device, &image, 0, &report) == -EIO);
  CHECK(!strcmp(report.stage, "verify") && report.failed_offset == 0);

  struct fake_nor* bad_erase = nor_new(0x00);
  bad_erase->fail_erase_at = kErase;
  device = nor_device(bad_erase);
  CHECK(flash_write_image(&device, &image, 0, &report) == -EIO);
  CHECK(!strcmp(report.stage, "erase") && report.failed_offset == kErase && report.written == 1);
  free(stuck);
  free(no_erase);
  free(bad_erase);
  free(bytes);
}

static void test_proc_mtd_parser(void) {
  const char* table =
      "dev:    size   erasesize  name\n"
      "mtd0: 00050000 00010000 \"BOOT0\"\n"
      "mtd1: 001f0000 00010000 \"KERNEL\"\n"
      "mtd2: 00450000 00010000 \"rootfs\"\n"
      "mtd3: 00800000 00010000 \"res\"\n"
      "mtd4: 000b0000 00010000 \"config\"\n"
      "mtd5: 00040000 00010000 \"MISC\"\n"
      "mtd6: 00800000 00010000 \"data\"\n"
      "mtd7: 00880000 00010000 \"UDISK\"\n";
  struct mtd_entry entry;
  CHECK(mtd_table_find(table, 3, &entry) == 0);
  CHECK(entry.size == 0x800000 && entry.erase_size == 0x10000 && !strcmp(entry.name, "res"));
  CHECK(mtd_table_find(table, 7, &entry) == 0 && !strcmp(entry.name, "UDISK"));
  CHECK(mtd_table_find(table, 8, &entry) == -ENOENT);
  CHECK(mtd_table_find("mtd3: 00800000 00010000 \"res\"\r\nmtd3: 1 1 \"x\"\n", 3, &entry) == -EINVAL);
  CHECK(mtd_table_find("mtd3: zz 00010000 \"res\"\n", 3, &entry) == -ENOENT);
  CHECK(mtd_table_find("mtd3: 00800000 00010000 \"res\"", 3, &entry) == 0);
  CHECK(mtd_table_find_name(table, "res", &entry) == 0 && entry.index == 3 && entry.size == 0x800000);
  CHECK(mtd_table_find_name(table, "UDISK", &entry) == 0 && entry.index == 7);
  CHECK(mtd_table_find_name(table, "re", &entry) == -ENOENT);
  CHECK(mtd_table_find_name("mtd3: 1 1 \"res\"\nmtd8: 1 1 \"res\"\n", "res", &entry) == -EINVAL);
}

static void squashfs_superblock(uint8_t* cells, uint64_t used) {
  memset(cells, 0, RELEASE_SLOT_SUPERBLOCK_BYTES);
  memcpy(cells, "hsqs", 4);
  for (int i = 0; i < 8; ++i) cells[40 + i] = (uint8_t)(used >> (8 * i));
}

static void test_release_slot_layout(void) {
  uint8_t superblock[RELEASE_SLOT_SUPERBLOCK_BYTES];
  struct release_slot_layout layout;
  squashfs_superblock(superblock, 5000);
  CHECK(release_slot_locate(superblock, kSize, kErase, &layout) == 0);
  CHECK(layout.header == 2 * kErase && layout.image == 3 * kErase && layout.capacity == kSize - 3 * kErase);
  squashfs_superblock(superblock, kErase);
  CHECK(release_slot_locate(superblock, kSize, kErase, &layout) == 0 && layout.header == kErase);
  squashfs_superblock(superblock, kSize - 2 * kErase);
  CHECK(release_slot_locate(superblock, kSize, kErase, &layout) == 0 && layout.capacity == kErase);
  squashfs_superblock(superblock, kSize - 2 * kErase + 1);
  CHECK(release_slot_locate(superblock, kSize, kErase, &layout) == -ENOSPC);
  squashfs_superblock(superblock, kSize + 1);
  CHECK(release_slot_locate(superblock, kSize, kErase, &layout) == -EINVAL);
  squashfs_superblock(superblock, 95);
  CHECK(release_slot_locate(superblock, kSize, kErase, &layout) == -EINVAL);
  squashfs_superblock(superblock, 5000);
  superblock[0] = 'x';
  CHECK(release_slot_locate(superblock, kSize, kErase, &layout) == -EINVAL);
  memset(superblock, 0xff, sizeof superblock);
  CHECK(release_slot_locate(superblock, kSize, kErase, &layout) == -EINVAL);
}

static void slot_image(uint8_t* bytes, size_t length, unsigned seed) {
  pattern(bytes, length, seed);
  squashfs_superblock(bytes, length);
}

static void test_release_slot_image(void) {
  uint8_t superblock[RELEASE_SLOT_SUPERBLOCK_BYTES];
  squashfs_superblock(superblock, 5000);
  CHECK(release_slot_image_valid(superblock, 5000));
  CHECK(release_slot_image_valid(superblock, 8192));
  CHECK(!release_slot_image_valid(superblock, 8193));
  CHECK(!release_slot_image_valid(superblock, 4999));
  superblock[1] = 0;
  CHECK(!release_slot_image_valid(superblock, 5000));
}

static struct release_slot_header slot_header(const uint8_t* image, size_t length, const char* release) {
  struct release_slot_header header;
  memset(&header, 0, sizeof header);
  header.image_bytes = length;
  header.counter = 1790545383;
  digest_of(image, length, header.image_sha256);
  snprintf(header.release, sizeof header.release, "%s", release);
  return header;
}

static void test_release_slot_header(void) {
  uint8_t image[100];
  pattern(image, sizeof image, 5);
  const struct release_slot_header header = slot_header(image, sizeof image, "1.1.2-g2bb7c23f9c5e-548e2c070fae");
  uint8_t bytes[RELEASE_SLOT_HEADER_BYTES];
  struct release_slot_header decoded;
  CHECK(release_slot_encode(&header, bytes) == 0);
  CHECK(!memcmp(bytes, RELEASE_SLOT_MAGIC, 8));
  CHECK(release_slot_decode(bytes, 100, &decoded) == 0);
  CHECK(decoded.image_bytes == 100 && decoded.counter == header.counter);
  CHECK(!memcmp(decoded.image_sha256, header.image_sha256, 32) && !strcmp(decoded.release, header.release));
  CHECK(release_slot_decode(bytes, 99, &decoded) == -EINVAL);
  for (size_t i = 0; i < sizeof bytes; ++i) {
    bytes[i] ^= 0x01;
    CHECK(release_slot_decode(bytes, kSize, &decoded) == -EINVAL);
    bytes[i] ^= 0x01;
  }
  uint8_t erased[RELEASE_SLOT_HEADER_BYTES];
  memset(erased, 0xff, sizeof erased);
  CHECK(release_slot_decode(erased, kSize, &decoded) == -EINVAL);
  memset(erased, 0x00, sizeof erased);
  CHECK(release_slot_decode(erased, kSize, &decoded) == -EINVAL);

  const char* names[] = {"", ".hidden", "a/b", "1.1.2.partial"};
  for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
    struct release_slot_header bad = slot_header(image, sizeof image, names[i]);
    CHECK(release_slot_encode(&bad, bytes) == -EINVAL);
  }
  struct release_slot_header bad = header;
  memset(bad.release, 'a', sizeof bad.release);
  CHECK(release_slot_encode(&bad, bytes) == -EINVAL);
  bad.release[TC002_RELEASE_NAME_LIMIT] = '\0';
  CHECK(release_slot_encode(&bad, bytes) == 0);
  bad = header;
  bad.counter = 0;
  CHECK(release_slot_encode(&bad, bytes) == -EINVAL);
  bad.counter = 0x8000000000000000ull;
  CHECK(release_slot_encode(&bad, bytes) == -EINVAL);
  bad = header;
  bad.image_bytes = 0;
  CHECK(release_slot_encode(&bad, bytes) == -EINVAL);
}

static void test_slot_write_and_read(void) {
  struct fake_nor* nor = nor_new(0xff);
  squashfs_superblock(nor->cells, 5000);
  struct flash_device device = nor_device(nor);
  const size_t length = kErase * 3 + 17;
  uint8_t* bytes = malloc(length);
  slot_image(bytes, length, 6);
  struct memory_image memory = {bytes};
  struct flash_source image = {.context = &memory, .length = length, .read = image_read};
  const struct release_slot_header header = slot_header(bytes, length, "release-a");
  struct flash_report report;
  struct release_slot_layout layout;
  struct release_slot_header read;
  CHECK(slot_read(&device, 1, &layout, &read) == -ENOENT);
  CHECK(slot_write(&device, &image, &header, &report) == 0);
  CHECK(report.modified && report.offset == 3 * kErase && !strcmp(report.stage, "done"));
  CHECK(slot_read(&device, 1, &layout, &read) == 0);
  CHECK(layout.image == 3 * kErase && read.image_bytes == length && !strcmp(read.release, "release-a"));
  CHECK(!memcmp(nor->cells + layout.image, bytes, length));
  nor->cells[layout.image + kErase + 3] ^= 0x10;
  CHECK(slot_read(&device, 1, &layout, &read) == -EBADMSG);
  CHECK(slot_read(&device, 0, &layout, &read) == 0);
  free(bytes);
  free(nor);
}

static void test_slot_write_checks_the_source_first(void) {
  struct fake_nor* nor = nor_new(0xff);
  squashfs_superblock(nor->cells, 5000);
  struct flash_device device = nor_device(nor);
  const size_t length = kErase * 2;
  uint8_t* bytes = malloc(length);
  slot_image(bytes, length, 7);
  struct memory_image memory = {bytes};
  struct flash_source image = {.context = &memory, .length = length, .read = image_read};
  const struct release_slot_header installed = slot_header(bytes, length, "release-a");
  struct flash_report report;
  CHECK(slot_write(&device, &image, &installed, &report) == 0);
  const unsigned erases = nor->erases, writes = nor->writes;

  struct release_slot_header wrong = slot_header(bytes, length, "release-b");
  wrong.image_sha256[0] ^= 1;
  CHECK(slot_write(&device, &image, &wrong, &report) == -EBADMSG);
  CHECK(!strcmp(report.stage, "source") && !report.modified);
  struct release_slot_header short_image = slot_header(bytes, length, "release-b");
  short_image.image_bytes = length - 1;
  CHECK(slot_write(&device, &image, &short_image, &report) == -EINVAL && !report.modified);
  uint8_t* plain = malloc(length);
  pattern(plain, length, 10);
  struct memory_image plain_memory = {plain};
  struct flash_source not_squashfs = {.context = &plain_memory, .length = length, .read = image_read};
  const struct release_slot_header plain_header = slot_header(plain, length, "release-b");
  CHECK(slot_write(&device, &not_squashfs, &plain_header, &report) == -EINVAL);
  CHECK(!strcmp(report.stage, "source") && !report.modified);
  free(plain);

  uint8_t* big = calloc(1, kSize);
  squashfs_superblock(big, kSize - 2 * kErase);
  struct memory_image large_memory = {big};
  struct flash_source large = {.context = &large_memory, .length = kSize - 2 * kErase, .read = image_read};
  const struct release_slot_header too_big = slot_header(big, kSize - 2 * kErase, "release-c");
  CHECK(slot_write(&device, &large, &too_big, &report) == -EFBIG && !strcmp(report.stage, "capacity"));
  CHECK(nor->erases == erases && nor->writes == writes);

  struct release_slot_layout layout;
  struct release_slot_header read;
  CHECK(slot_read(&device, 1, &layout, &read) == 0 && !strcmp(read.release, "release-a"));
  free(big);
  free(bytes);
  free(nor);
}

static void test_interrupted_slot_write_leaves_no_slot(void) {
  struct fake_nor* nor = nor_new(0xff);
  squashfs_superblock(nor->cells, 5000);
  struct flash_device device = nor_device(nor);
  const size_t length = kErase * 3;
  uint8_t* bytes = malloc(length);
  slot_image(bytes, length, 8);
  struct memory_image memory = {bytes};
  struct flash_source image = {.context = &memory, .length = length, .read = image_read};
  const struct release_slot_header first = slot_header(bytes, length, "release-a");
  struct flash_report report;
  CHECK(slot_write(&device, &image, &first, &report) == 0);
  slot_image(bytes, length, 9);
  const struct release_slot_header second = slot_header(bytes, length, "release-b");
  nor->fail_erase_at = 4 * kErase;
  CHECK(slot_write(&device, &image, &second, &report) == -EIO);
  CHECK(report.modified && !strcmp(report.stage, "erase"));
  struct release_slot_layout layout;
  struct release_slot_header read;
  CHECK(slot_read(&device, 0, &layout, &read) == -ENOENT);
  nor->fail_erase_at = -1;
  CHECK(slot_write(&device, &image, &second, &report) == 0);
  CHECK(slot_read(&device, 1, &layout, &read) == 0 && !strcmp(read.release, "release-b"));

  squashfs_superblock(nor->cells, 5000);
  nor->cells[0] = 0;
  CHECK(slot_read(&device, 0, &layout, &read) == -EINVAL);
  CHECK(slot_write(&device, &image, &second, &report) == -EINVAL && !report.modified);
  free(bytes);
  free(nor);
}

static void test_deploy_token_staleness(void) {
  const double stale = AWTRIX_DEPLOY_TOKEN_STALE_SECONDS;
  struct deploy_token_watch watch = {0};
  CHECK(deploy_token_fresh(&watch, 1000, 5, 50.0, 0.2, stale));
  CHECK(deploy_token_fresh(&watch, 1000, 5, 50.0 + stale - 0.3, 1e9, stale));
  CHECK(!deploy_token_fresh(&watch, 1000, 5, 50.0 + stale + 0.1, 0.0, stale));
  CHECK(deploy_token_fresh(&watch, 1000, 6, 50.0 + stale + 0.2, 1e9, stale));
  CHECK(deploy_token_fresh(&watch, 1000, 6, 50.0 + 2 * stale, -1e9, stale));
  CHECK(!deploy_token_fresh(&watch, 1000, 6, 50.0 + 2 * stale + 0.3, 0.0, stale));
  CHECK(deploy_token_fresh(&watch, 999, 6, 50.0 + 3 * stale, 0.0, stale));

  struct deploy_token_watch old = {0};
  CHECK(!deploy_token_fresh(&old, 7, 0, 10.0, stale + 1, stale));
  CHECK(!deploy_token_fresh(&old, 7, 0, 11.0, 0.0, stale));
  CHECK(deploy_token_fresh(&old, 8, 0, 12.0, stale + 1, stale));

  struct deploy_token_watch aging = {0};
  CHECK(deploy_token_fresh(&aging, 7, 0, 10.0, stale - 10, stale));
  CHECK(deploy_token_fresh(&aging, 7, 0, 19.5, 0.0, stale));
  CHECK(!deploy_token_fresh(&aging, 7, 0, 20.0, 0.0, stale));

  struct deploy_token_watch future = {0};
  CHECK(deploy_token_fresh(&future, 7, 0, 10.0, -3600.0, stale));
  CHECK(deploy_token_fresh(&future, 7, 0, 10.0 + stale - 1, -3600.0, stale));
  CHECK(!deploy_token_fresh(&future, 7, 0, 10.0 + stale, -3600.0, stale));
  CHECK(!deploy_token_fresh(&future, 7, 0, 10.0 + stale, 0.0, 0.0));
}

static void test_deploy_token_uptime(void) {
  double uptime = -1;
  CHECK(deploy_token_uptime("deploy 123.45\n", &uptime) && uptime > 123.449 && uptime < 123.451);
  CHECK(deploy_token_uptime("deploy 7", &uptime) && uptime == 7.0);
  CHECK(deploy_token_uptime("deploy 0.05\n", &uptime) && uptime > 0.049 && uptime < 0.051);
  const char* rejected[] = {"deploy\n", "deploy \n", "deploy .5\n", "deploy 1.2.3\n",
                            "deploy 12x\n", "deploy 12\nmore", "deploy -3\n", "Deploy 1\n",
                            "deploy 1234567890123456\n", ""};
  for (size_t i = 0; i < sizeof rejected / sizeof rejected[0]; ++i) {
    uptime = -1;
    CHECK(!deploy_token_uptime(rejected[i], &uptime) && uptime == -1);
  }
  CHECK(deploy_token_age("deploy 100.00\n", 130.0, 1e9) == 30.0);
  CHECK(deploy_token_age("deploy 100.00\n", 90.0, 5.0) == -10.0);
  CHECK(deploy_token_age("deploy\n", 130.0, 5.0) == 5.0);

  const double stale = AWTRIX_DEPLOY_TOKEN_STALE_SECONDS;
  struct deploy_token_watch stepped = {0};
  const double age = deploy_token_age("deploy 40.00\n", 50.0, 56.0 * 365 * 86400);
  CHECK(deploy_token_fresh(&stepped, 1790000000, 0, 1000.0, age, stale));
  CHECK(deploy_token_fresh(&stepped, 1790000000, 0, 1000.0 + stale - 10.5, 0.0, stale));
  CHECK(!deploy_token_fresh(&stepped, 1790000000, 0, 1000.0 + stale - 10.0, 0.0, stale));

  struct deploy_token_watch old = {0};
  CHECK(!deploy_token_fresh(&old, 5, 0, 1000.0, deploy_token_age("deploy 10.00\n", 200.0, 0.0),
                            stale));
}

int main(void) {
  test_sha256_vectors();
  test_blank_device_written_and_tail_padded();
  test_identical_blocks_are_skipped();
  test_chunked_offsets_compose();
  test_requests_rejected_before_modification();
  test_faults_are_reported();
  test_proc_mtd_parser();
  test_release_slot_layout();
  test_release_slot_image();
  test_release_slot_header();
  test_slot_write_and_read();
  test_slot_write_checks_the_source_first();
  test_interrupted_slot_write_leaves_no_slot();
  test_deploy_token_staleness();
  test_deploy_token_uptime();
  if (awtrix_test_failures) {
    fprintf(stderr, "%d check(s) failed\n", awtrix_test_failures);
    return 1;
  }
  puts("flasher tests passed");
  return 0;
}
