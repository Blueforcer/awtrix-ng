#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "../contract/deploy_token_file.h"
#include "../contract/tc002_layout.h"
#include "flash_write.h"
#include "mtd_device.h"
#include "slot_io.h"
#include "../../posix/sha256.h"

#ifndef AWTRIX_FLASH_VERSION
#define AWTRIX_FLASH_VERSION "dev"
#endif

enum { kExitOk = 0, kExitUnchanged = 1, kExitModified = 2, kExitUsage = 64 };

static void json_string(const char* text) {
  putchar('"');
  for (const unsigned char* c = (const unsigned char*)text; *c; ++c) {
    if (*c == '"' || *c == '\\') printf("\\%c", *c);
    else if (*c < 0x20) printf("\\u%04x", *c);
    else putchar(*c);
  }
  putchar('"');
}

static void begin(const char* command, const char* result) {
  printf("{\"command\":\"%s\",\"result\":\"%s\"", command, result);
}

static int report_error(const char* command, const char* stage, int error, const char* message,
                        int modified) {
  begin(command, "error");
  printf(",\"stage\":\"%s\",\"errno\":%d,\"modified\":%s,\"message\":", stage, error,
         modified ? "true" : "false");
  json_string(message);
  puts("}");
  fprintf(stderr, "awtrix-tc002-flash %s: %s\n", command, message);
  return modified ? kExitModified : kExitUnchanged;
}

static int usage(void) {
  fputs("usage: awtrix-tc002-flash version\n"
        "       awtrix-tc002-flash hash PATH [LENGTH]\n"
        "       awtrix-tc002-flash sha256 FILE...\n"
        "       awtrix-tc002-flash write DEVICE IMAGE [--offset BYTES] [--name NAME]\n"
        "       awtrix-tc002-flash slot-info [--device DEVICE] [--verify]\n"
        "       awtrix-tc002-flash write-slot SOURCE --length BYTES --sha256 HEX --release NAME\n"
        "                          --counter N [--offset BYTES] [--device DEVICE] [--mount DIR]\n"
        "                          [--result FILE] [--reboot]\n"
        "       awtrix-tc002-flash statfs PATH\n"
        "       awtrix-tc002-flash symlink TARGET LINK\n"
        "       awtrix-tc002-flash lock DIRECTORY TOKEN SECONDS STALE_SECONDS\n",
        stderr);
  return kExitUsage;
}

static int parse_size(const char* text, uint64_t* value) {
  if (!text || !*text || *text == '-') return -1;
  char* end;
  errno = 0;
  const int hex = text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
  const unsigned long long parsed = strtoull(text, &end, hex ? 16 : 10);
  if (errno || *end) return -1;
  *value = parsed;
  return 0;
}

static int file_read(void* context, uint64_t offset, void* buffer, size_t length) {
  return fd_read_exact(*(const int*)context, offset, buffer, length);
}

static int hash_fd(int fd, uint64_t length, uint8_t digest[32]) {
  struct flash_device device = {.context = &fd, .size = length, .read = file_read};
  return flash_hash_range(&device, 0, length, digest);
}

static int command_hash(int argc, char** argv) {
  if (argc < 1 || argc > 2) return usage();
  const char* path = argv[0];
  uint64_t requested = 0;
  if (argc == 2 && parse_size(argv[1], &requested)) return usage();
  struct stat info;
  if (stat(path, &info)) return report_error("hash", "open", errno, strerror(errno), 0);
  uint8_t digest[32];
  char hex[65];
  if (S_ISCHR(info.st_mode)) {
    struct mtd_handle mtd;
    char error[160];
    if (mtd_open(path, 0, &mtd, error, sizeof error))
      return report_error("hash", "open", EINVAL, error, 0);
    const uint64_t length = argc == 2 ? requested : mtd.device.size;
    const int result = flash_hash_range(&mtd.device, 0, length, digest);
    mtd_close(&mtd);
    if (result) return report_error("hash", "read", -result, strerror(-result), 0);
    sha256_hex(digest, hex);
    begin("hash", "ok");
    printf(",\"path\":");
    json_string(path);
    printf(",\"name\":");
    json_string(mtd.entry.name);
    printf(",\"size\":%" PRIu64 ",\"erase_size\":%" PRIu32 ",\"length\":%" PRIu64
           ",\"sha256\":\"%s\"}\n",
           mtd.device.size, mtd.device.erase_size, length, hex);
    return kExitOk;
  }
  if (!S_ISREG(info.st_mode)) return report_error("hash", "open", EINVAL, "not a file", 0);
  const uint64_t length = argc == 2 ? requested : (uint64_t)info.st_size;
  if (length > (uint64_t)info.st_size)
    return report_error("hash", "length", EFBIG, "length exceeds the file", 0);
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return report_error("hash", "open", errno, strerror(errno), 0);
  const int result = hash_fd(fd, length, digest);
  close(fd);
  if (result) return report_error("hash", "read", -result, strerror(-result), 0);
  sha256_hex(digest, hex);
  begin("hash", "ok");
  printf(",\"path\":");
  json_string(path);
  printf(",\"size\":%" PRIu64 ",\"length\":%" PRIu64 ",\"sha256\":\"%s\"}\n",
         (uint64_t)info.st_size, length, hex);
  return kExitOk;
}

static int command_sha256(int argc, char** argv) {
  if (argc < 1) return usage();
  int status = kExitOk;
  for (int i = 0; i < argc; ++i) {
    struct stat info;
    uint8_t digest[32];
    char hex[65];
    const int fd = open(argv[i], O_RDONLY | O_CLOEXEC);
    int result = fd < 0 ? -errno : 0;
    if (!result && (fstat(fd, &info) || !S_ISREG(info.st_mode))) result = -EINVAL;
    if (!result) result = hash_fd(fd, (uint64_t)info.st_size, digest);
    if (fd >= 0) close(fd);
    if (result) {
      begin("sha256", "error");
      printf(",\"path\":");
      json_string(argv[i]);
      printf(",\"errno\":%d}\n", -result);
      status = kExitUnchanged;
      continue;
    }
    sha256_hex(digest, hex);
    begin("sha256", "ok");
    printf(",\"path\":");
    json_string(argv[i]);
    printf(",\"size\":%" PRIu64 ",\"mode\":\"%04o\",\"uid\":%u,\"gid\":%u,\"sha256\":\"%s\"}\n",
           (uint64_t)info.st_size, (unsigned)(info.st_mode & 07777), (unsigned)info.st_uid,
           (unsigned)info.st_gid, hex);
  }
  return status;
}

static int command_write(int argc, char** argv) {
  if (argc < 2) return usage();
  const char* device_path = argv[0];
  const char* image_path = argv[1];
  const char* expected_name = "res";
  uint64_t offset = 0;
  for (int i = 2; i < argc; i += 2) {
    if (i + 1 >= argc) return usage();
    if (!strcmp(argv[i], "--offset")) {
      if (parse_size(argv[i + 1], &offset)) return usage();
    } else if (!strcmp(argv[i], "--name")) {
      expected_name = argv[i + 1];
    } else {
      return usage();
    }
  }
  int image = open(image_path, O_RDONLY | O_CLOEXEC);
  struct stat info;
  if (image < 0) return report_error("write", "image", errno, strerror(errno), 0);
  if (fstat(image, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0) {
    close(image);
    return report_error("write", "image", EINVAL, "image is not a non-empty regular file", 0);
  }
  struct mtd_handle mtd;
  char error[160];
  if (mtd_open(device_path, 1, &mtd, error, sizeof error)) {
    close(image);
    return report_error("write", "open", EINVAL, error, 0);
  }
  const char* refusal = NULL;
  if (strcmp(mtd.entry.name, expected_name)) refusal = "partition name does not match --name";
  else if (!mtd.nor) refusal = "partition is not NOR flash";
  else if (!mtd.writeable) refusal = "partition is not writeable";
  else if (mtd.write_size > mtd.device.erase_size) refusal = "unsupported write size";
  if (refusal) {
    mtd_close(&mtd);
    close(image);
    return report_error("write", "geometry", EINVAL, refusal, 0);
  }
  struct flash_source source = {.context = &image, .length = (uint64_t)info.st_size,
                                .read = file_read};
  struct flash_report report;
  const int result = flash_write_image(&mtd.device, &source, offset, &report);
  mtd_close(&mtd);
  close(image);
  if (result) {
    char message[160];
    snprintf(message, sizeof message, "%s failed at offset %" PRIu64 ": %s", report.stage,
             report.failed_offset, strerror(report.error));
    return report_error("write", report.stage, report.error, message, report.modified);
  }
  char hex[65];
  sha256_hex(report.sha256, hex);
  begin("write", "ok");
  printf(",\"device\":");
  json_string(device_path);
  printf(",\"name\":");
  json_string(mtd.entry.name);
  printf(",\"offset\":%" PRIu64 ",\"length\":%" PRIu64 ",\"span\":%" PRIu64
         ",\"blocks\":%" PRIu32 ",\"skipped\":%" PRIu32 ",\"written\":%" PRIu32
         ",\"retries\":%" PRIu32 ",\"modified\":%s,\"sha256\":\"%s\"}\n",
         report.offset, report.length, report.span, report.blocks, report.skipped, report.written,
         report.retries, report.modified ? "true" : "false", hex);
  return kExitOk;
}

static int parse_hex(const char* text, uint8_t digest[32]) {
  if (!text || strlen(text) != 64) return -1;
  for (int i = 0; i < 32; ++i) {
    unsigned value = 0;
    for (int j = 0; j < 2; ++j) {
      const char c = text[2 * i + j];
      const int nibble = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
      if (nibble < 0) return -1;
      value = value << 4 | (unsigned)nibble;
    }
    digest[i] = (uint8_t)value;
  }
  return 0;
}

// The res partition, by name unless a device is given; for writing, only NOR that the kernel lets
// us write with erase blocks at least as large as its write size.
static int open_res(const char* device, int for_write, struct mtd_handle* mtd, char* error,
                    size_t size) {
  char path[64];
  if (!device) {
    const int found = mtd_device_path(TC002_MTD_DIR, TC002_RES_PARTITION, for_write, path,
                                      sizeof path, NULL);
    if (found) {
      snprintf(error, size, "no %s partition: %s", TC002_RES_PARTITION, strerror(-found));
      return -1;
    }
    device = path;
  }
  if (mtd_open(device, for_write, mtd, error, size)) return -1;
  const char* refusal = NULL;
  if (strcmp(mtd->entry.name, TC002_RES_PARTITION)) refusal = "the device is not the res partition";
  else if (for_write && !mtd->nor) refusal = "res is not NOR flash";
  else if (for_write && !mtd->writeable) refusal = "res is not writeable";
  else if (for_write && mtd->write_size > mtd->device.erase_size) refusal = "unsupported write size";
  if (!refusal) return 0;
  snprintf(error, size, "%s", refusal);
  mtd_close(mtd);
  return -1;
}

static const char* slot_layout_error(int result) {
  return result == -ENOSPC ? "res leaves no room for a release slot"
                           : "res does not start with a squashfs";
}

static void print_header(const struct release_slot_header* header) {
  char hex[65];
  sha256_hex(header->image_sha256, hex);
  printf(",\"release\":");
  json_string(header->release);
  printf(",\"counter\":%" PRIu64 ",\"imageBytes\":%" PRIu64 ",\"sha256\":\"%s\"", header->counter,
         header->image_bytes, hex);
}

static int command_slot_info(int argc, char** argv) {
  const char* device = NULL;
  int verify = 0;
  for (int i = 0; i < argc; ++i) {
    if (!strcmp(argv[i], "--verify")) verify = 1;
    else if (!strcmp(argv[i], "--device") && i + 1 < argc) device = argv[++i];
    else return usage();
  }
  struct mtd_handle mtd;
  char error[160];
  if (open_res(device, 0, &mtd, error, sizeof error))
    return report_error("slot-info", "open", EINVAL, error, 0);
  struct release_slot_layout layout;
  struct release_slot_header header;
  const int result = slot_read(&mtd.device, verify, &layout, &header);
  mtd_close(&mtd);
  if (result == -EINVAL || result == -ENOSPC)
    return report_error("slot-info", "layout", -result, slot_layout_error(result), 0);
  if (result && result != -ENOENT && result != -EBADMSG)
    return report_error("slot-info", "read", -result, strerror(-result), 0);
  const char* state = result == -ENOENT ? "empty" : result == -EBADMSG ? "corrupt"
                      : verify          ? "verified" : "valid";
  begin("slot-info", "ok");
  printf(",\"header\":%" PRIu64 ",\"image\":%" PRIu64 ",\"capacity\":%" PRIu64 ",\"slot\":\"%s\"",
         layout.header, layout.image, layout.capacity, state);
  if (result != -ENOENT) print_header(&header);
  puts("}");
  return kExitOk;
}

struct range {
  int fd;
  uint64_t offset;
};

static int range_read(void* context, uint64_t offset, void* buffer, size_t length) {
  const struct range* range = context;
  return fd_read_exact(range->fd, range->offset + offset, buffer, length);
}

static void record_result(const char* path, const char* line) {
  if (!path) return;
  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0) return;
  if (write(fd, line, strlen(line)) >= 0) fsync(fd);
  close(fd);
  tc002_sync_parent(path);
}

/* Drops the clean page cache before the next slot mount. */
static int drop_page_cache(void) {
  const int fd = open("/proc/sys/vm/drop_caches", O_WRONLY | O_CLOEXEC);
  if (fd < 0) return -errno;
  const int written = write(fd, "1", 1) == 1;
  const int error = errno;
  close(fd);
  return written ? 0 : -error;
}

static void finish_write_slot(const char* result_file, int reboot_after, int modified,
                              const char* line) {
  record_result(result_file, line);
  if (!reboot_after || !modified) return;
  fflush(NULL);
  sync();
  reboot(RB_AUTOBOOT);
}

static int command_write_slot(int argc, char** argv) {
  if (argc < 1) return usage();
  const char* source_path = argv[0];
  const char* device = NULL;
  const char* mount_point = TC002_VOLATILE_DIR TC002_RELEASE_MOUNT;
  const char* result_file = NULL;
  const char* release = NULL;
  const char* sha256 = NULL;
  uint64_t offset = 0, length = 0, counter = 0;
  int reboot_after = 0;
  for (int i = 1; i < argc; ++i) {
    const char* option = argv[i];
    if (!strcmp(option, "--reboot")) {
      reboot_after = 1;
      continue;
    }
    if (i + 1 >= argc) return usage();
    const char* value = argv[++i];
    if (!strcmp(option, "--length")) {
      if (parse_size(value, &length) || !length) return usage();
    } else if (!strcmp(option, "--offset")) {
      if (parse_size(value, &offset)) return usage();
    } else if (!strcmp(option, "--counter")) {
      if (parse_size(value, &counter)) return usage();
    } else if (!strcmp(option, "--sha256")) {
      sha256 = value;
    } else if (!strcmp(option, "--release")) {
      release = value;
    } else if (!strcmp(option, "--device")) {
      device = value;
    } else if (!strcmp(option, "--mount")) {
      mount_point = value;
    } else if (!strcmp(option, "--result")) {
      result_file = value;
    } else {
      return usage();
    }
  }
  struct release_slot_header header;
  memset(&header, 0, sizeof header);
  if (!length || !release || strlen(release) >= sizeof header.release ||
      parse_hex(sha256, header.image_sha256))
    return usage();
  header.image_bytes = length;
  header.counter = counter;
  memcpy(header.release, release, strlen(release));
  if (chdir("/")) {
  }
  char line[512];
  int status;
  const int source = open(source_path, O_RDONLY | O_CLOEXEC);
  struct stat info;
  struct mtd_handle mtd;
  char error[160];
  if (source < 0 || fstat(source, &info) || !S_ISREG(info.st_mode) ||
      (uint64_t)info.st_size < offset || (uint64_t)info.st_size - offset < length) {
    snprintf(error, sizeof error, "%s does not hold %" PRIu64 " bytes at %" PRIu64, source_path,
             length, offset);
    if (source >= 0) close(source);
    status = report_error("write-slot", "source", EINVAL, error, 0);
    snprintf(line, sizeof line, "source: %s\n", error);
    finish_write_slot(result_file, reboot_after, 0, line);
    return status;
  }
  if (open_res(device, 1, &mtd, error, sizeof error)) {
    close(source);
    status = report_error("write-slot", "open", EINVAL, error, 0);
    snprintf(line, sizeof line, "open: %s\n", error);
    finish_write_slot(result_file, reboot_after, 0, line);
    return status;
  }
  if (tc002_mounted(mount_point) && umount(mount_point)) {
    const int busy = errno;
    snprintf(error, sizeof error, "cannot unmount the installed release at %s: %s", mount_point,
             strerror(busy));
    mtd_close(&mtd);
    close(source);
    status = report_error("write-slot", "unmount", busy, error, 0);
    snprintf(line, sizeof line, "unmount: %s\n", error);
    finish_write_slot(result_file, reboot_after, 0, line);
    return status;
  }
  struct range range = {source, offset};
  const struct flash_source image = {&range, length, range_read};
  struct flash_report report;
  const int result = slot_write(&mtd.device, &image, &header, &report);
  mtd_close(&mtd);
  const int cache = report.modified ? drop_page_cache() : 0;
  close(source);
  if (result) {
    snprintf(error, sizeof error, "%s failed at offset %" PRIu64 ": %s", report.stage,
             report.failed_offset, strerror(report.error));
    status = report_error("write-slot", report.stage, report.error, error, report.modified);
    snprintf(line, sizeof line, "%s: %s\n", report.stage, error);
    finish_write_slot(result_file, reboot_after, report.modified, line);
    return status;
  }
  begin("write-slot", "ok");
  printf(",\"image\":%" PRIu64 ",\"blocks\":%" PRIu32 ",\"skipped\":%" PRIu32 ",\"written\":%" PRIu32,
         report.offset, report.blocks, report.skipped, report.written);
  printf(",\"blockCache\":");
  json_string(cache ? strerror(-cache) : "dropped");
  print_header(&header);
  puts("}");
  snprintf(line, sizeof line, "done: %s\n", header.release);
  finish_write_slot(result_file, reboot_after, 1, line);
  return kExitOk;
}

static int command_statfs(int argc, char** argv) {
  if (argc != 1) return usage();
  struct statvfs info;
  if (statvfs(argv[0], &info)) return report_error("statfs", "statfs", errno, strerror(errno), 0);
  begin("statfs", "ok");
  printf(",\"path\":");
  json_string(argv[0]);
  printf(",\"block_size\":%lu,\"free_bytes\":%" PRIu64 ",\"total_bytes\":%" PRIu64 "}\n",
         (unsigned long)info.f_frsize, (uint64_t)info.f_bavail * info.f_frsize,
         (uint64_t)info.f_blocks * info.f_frsize);
  return kExitOk;
}

static int command_symlink(int argc, char** argv) {
  if (argc != 2) return usage();
  const char* target = argv[0];
  const char* link = argv[1];
  struct stat info;
  if (!lstat(link, &info) && !S_ISLNK(info.st_mode))
    return report_error("symlink", "check", EEXIST, "link path exists and is not a symlink", 0);
  char temporary[PATH_MAX];
  if (snprintf(temporary, sizeof temporary, "%s.new-%ld", link, (long)getpid()) >=
      (int)sizeof temporary)
    return report_error("symlink", "check", ENAMETOOLONG, "link path too long", 0);
  unlink(temporary);
  if (symlink(target, temporary))
    return report_error("symlink", "create", errno, strerror(errno), 0);
  if (rename(temporary, link)) {
    const int error = errno;
    unlink(temporary);
    return report_error("symlink", "rename", error, strerror(error), 0);
  }
  const int synced = tc002_sync_parent(link);
  if (synced) return report_error("symlink", "sync", -synced, strerror(-synced), 1);
  begin("symlink", "ok");
  printf(",\"link\":");
  json_string(link);
  printf(",\"target\":");
  json_string(target);
  puts("}");
  return kExitOk;
}

static void __attribute__((noreturn)) hold_lock(const char* token, uint64_t seconds, uint64_t stale,
                                                struct deploy_token_watch watch) {
  setsid();
  const int null = open("/dev/null", O_RDWR | O_CLOEXEC);
  for (int fd = 0; fd <= 2 && null >= 0; ++fd) dup2(null, fd);
  if (null > 2) close(null);
  if (chdir("/")) {
  }
  const double end = tc002_clock_seconds(CLOCK_MONOTONIC) + (double)seconds;
  const struct timespec interval = {0, 200 * 1000 * 1000};
  while (tc002_clock_seconds(CLOCK_MONOTONIC) < end && deploy_token_file_active(token, &watch, (double)stale, 0))
    nanosleep(&interval, NULL);
  _exit(0);
}

static int command_lock(int argc, char** argv) {
  if (argc != 4) return usage();
  const char* directory = argv[0];
  const char* token = argv[1];
  uint64_t seconds = 0, stale = 0;
  if (parse_size(argv[2], &seconds) || !seconds || seconds > 86400) return usage();
  if (parse_size(argv[3], &stale) || !stale || stale > 86400) return usage();
  struct stat info;
  if (stat(token, &info)) return report_error("lock", "token", errno, strerror(errno), 0);
  struct deploy_token_watch watch = {0};
  if (!deploy_token_file_active(token, &watch, (double)stale, 0))
    return report_error("lock", "token", ESTALE, "the token is older than the staleness window", 0);
  if (mkdir(directory, 0700) && errno != EEXIST)
    return report_error("lock", "directory", errno, strerror(errno), 0);
  const int parent = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (parent < 0) return report_error("lock", "directory", errno, strerror(errno), 0);
  const int fd = openat(parent, "lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
  const int opened = errno;
  close(parent);
  if (fd < 0) return report_error("lock", "open", opened, strerror(opened), 0);
  if (flock(fd, LOCK_EX | LOCK_NB)) {
    const int error = errno;
    close(fd);
    if (error == EWOULDBLOCK)
      return report_error("lock", "busy", error, "an update holds the lock", 0);
    return report_error("lock", "flock", error, strerror(error), 0);
  }
  fflush(NULL);
  signal(SIGHUP, SIG_IGN);
  const pid_t holder = fork();
  if (holder < 0) {
    const int error = errno;
    close(fd);
    return report_error("lock", "fork", error, strerror(error), 0);
  }
  if (holder == 0) hold_lock(token, seconds, stale, watch);
  close(fd);
  begin("lock", "ok");
  printf(",\"path\":");
  json_string(directory);
  printf(",\"pid\":%ld,\"seconds\":%" PRIu64 ",\"stale\":%" PRIu64, (long)holder, seconds, stale);
  puts("}");
  return kExitOk;
}

int main(int argc, char** argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  if (argc < 2) return usage();
  const char* command = argv[1];
  if (!strcmp(command, "version") && argc == 2) {
    begin("version", "ok");
    printf(",\"version\":\"%s\"}\n", AWTRIX_FLASH_VERSION);
    return kExitOk;
  }
  if (!strcmp(command, "hash")) return command_hash(argc - 2, argv + 2);
  if (!strcmp(command, "sha256")) return command_sha256(argc - 2, argv + 2);
  if (!strcmp(command, "write")) return command_write(argc - 2, argv + 2);
  if (!strcmp(command, "slot-info")) return command_slot_info(argc - 2, argv + 2);
  if (!strcmp(command, "write-slot")) return command_write_slot(argc - 2, argv + 2);
  if (!strcmp(command, "statfs")) return command_statfs(argc - 2, argv + 2);
  if (!strcmp(command, "symlink")) return command_symlink(argc - 2, argv + 2);
  if (!strcmp(command, "lock")) return command_lock(argc - 2, argv + 2);
  return usage();
}
