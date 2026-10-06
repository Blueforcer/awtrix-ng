#include "mtd_device.h"

#include <errno.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define MTD_CHAR_MAJOR_NUMBER 90

int fd_read_exact(int fd, uint64_t offset, void* buffer, size_t length) {
  uint8_t* bytes = buffer;
  while (length) {
    const ssize_t count = pread(fd, bytes, length, (off_t)offset);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) return -errno;
    if (count == 0) return -EIO;
    bytes += count;
    offset += (uint64_t)count;
    length -= (size_t)count;
  }
  return 0;
}

int fd_write_exact(int fd, uint64_t offset, const void* buffer, size_t length) {
  const uint8_t* bytes = buffer;
  while (length) {
    const ssize_t count = pwrite(fd, bytes, length, (off_t)offset);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) return -errno;
    if (count == 0) return -EIO;
    bytes += count;
    offset += (uint64_t)count;
    length -= (size_t)count;
  }
  return 0;
}

static int mtd_erase(void* context, uint64_t offset, uint32_t length) {
  const struct mtd_handle* handle = context;
  if (offset > UINT32_MAX) return -EINVAL;
  struct erase_info_user request = {.start = (uint32_t)offset, .length = length};
  return ioctl(handle->fd, MEMERASE, &request) ? -errno : 0;
}

static int mtd_read(void* context, uint64_t offset, void* buffer, size_t length) {
  const struct mtd_handle* handle = context;
  return fd_read_exact(handle->fd, offset, buffer, length);
}

static int mtd_write(void* context, uint64_t offset, const void* buffer, size_t length) {
  const struct mtd_handle* handle = context;
  return fd_write_exact(handle->fd, offset, buffer, length);
}

static int read_proc_mtd(char* text, size_t size) {
  const int fd = open("/proc/mtd", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -errno;
  size_t used = 0;
  while (used + 1 < size) {
    const ssize_t count = read(fd, text + used, size - 1 - used);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    used += (size_t)count;
  }
  close(fd);
  text[used] = '\0';
  return used + 1 < size ? 0 : -EFBIG;
}

int mtd_open(const char* path, int for_write, struct mtd_handle* handle, char* error,
             size_t error_size) {
  memset(handle, 0, sizeof *handle);
  handle->fd = open(path, (for_write ? O_RDWR : O_RDONLY) | O_CLOEXEC);
  if (handle->fd < 0) {
    snprintf(error, error_size, "cannot open %s: %s", path, strerror(errno));
    return -1;
  }
  struct stat info;
  if (fstat(handle->fd, &info) || !S_ISCHR(info.st_mode) ||
      major(info.st_rdev) != MTD_CHAR_MAJOR_NUMBER || (for_write && (minor(info.st_rdev) & 1))) {
    snprintf(error, error_size, "%s is not a %sMTD character device", path,
             for_write ? "read-write " : "");
    goto fail;
  }
  const unsigned index = minor(info.st_rdev) >> 1;
  struct mtd_info_user mtd;
  if (ioctl(handle->fd, MEMGETINFO, &mtd)) {
    snprintf(error, error_size, "MEMGETINFO failed on %s: %s", path, strerror(errno));
    goto fail;
  }
  char table[4096];
  if (read_proc_mtd(table, sizeof table) || mtd_table_find(table, index, &handle->entry)) {
    snprintf(error, error_size, "/proc/mtd has no unique entry for mtd%u", index);
    goto fail;
  }
  if (handle->entry.size != mtd.size || handle->entry.erase_size != mtd.erasesize) {
    snprintf(error, error_size, "MEMGETINFO geometry of mtd%u disagrees with /proc/mtd", index);
    goto fail;
  }
  handle->nor = mtd.type == MTD_NORFLASH;
  handle->writeable = (mtd.flags & MTD_WRITEABLE) != 0;
  handle->write_size = mtd.writesize;
  handle->device.context = handle;
  handle->device.size = mtd.size;
  handle->device.erase_size = mtd.erasesize;
  handle->device.erase = mtd_erase;
  handle->device.read = mtd_read;
  handle->device.write = mtd_write;
  return 0;
fail:
  close(handle->fd);
  handle->fd = -1;
  return -1;
}

int mtd_device_path(const char* dir, const char* name, int for_write, char* path, size_t size,
                    unsigned* index) {
  char table[4096];
  struct mtd_entry entry;
  int result = read_proc_mtd(table, sizeof table);
  if (!result) result = mtd_table_find_name(table, name, &entry);
  if (result) return result;
  if (snprintf(path, size, "%s/mtd%u%s", dir, entry.index, for_write ? "" : "ro") >= (int)size)
    return -ENAMETOOLONG;
  if (index) *index = entry.index;
  return 0;
}

void mtd_close(struct mtd_handle* handle) {
  if (handle->fd >= 0) close(handle->fd);
  handle->fd = -1;
}
