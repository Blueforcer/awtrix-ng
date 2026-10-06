#include "../contract/posix_files.h"
#include "loader_state.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "loader_policy.h"

static int write_all(int fd, const char* text, size_t length) {
  while (length) {
    const ssize_t count = write(fd, text, length);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return count < 0 ? -errno : -EIO;
    text += count;
    length -= (size_t)count;
  }
  return 0;
}

int loader_read_attempts(const char* path) {
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return errno == ENOENT ? 0 : -1;
  char text[16];
  ssize_t count;
  do count = read(fd, text, sizeof text);
  while (count < 0 && errno == EINTR);
  close(fd);
  if (count < 0) return -1;
  return loader_parse_attempts(text, (size_t)count);
}

int loader_store_attempts(const char* path, int attempts) {
  char temporary[PATH_MAX];
  char text[16];
  if (snprintf(temporary, sizeof temporary, "%s.new", path) >= (int)sizeof temporary)
    return -ENAMETOOLONG;
  const int length = snprintf(text, sizeof text, "%d\n", attempts);
  const int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) return -errno;
  int result = write_all(fd, text, (size_t)length);
  if (!result && fsync(fd)) result = -errno;
  close(fd);
  if (!result && rename(temporary, path)) result = -errno;
  if (result) {
    unlink(temporary);
    return result;
  }
  return tc002_sync_parent(path);
}

int loader_clear_attempts(const char* path) {
  if (unlink(path)) return errno == ENOENT ? 0 : -errno;
  return tc002_sync_parent(path);
}

int loader_append_log(const char* path, const char* text, size_t limit, int durable) {
  const size_t length = strlen(text);
  struct stat info;
  int renamed = 0;
  const int existed = stat(path, &info) == 0;
  if (existed && (size_t)info.st_size + length > limit) {
    char previous[PATH_MAX];
    if (snprintf(previous, sizeof previous, "%s.1", path) >= (int)sizeof previous)
      return -ENAMETOOLONG;
    if (rename(path, previous)) return -errno;
    renamed = 1;
  }
  const int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0) return -errno;
  int result = write_all(fd, text, length);
  if (!result && durable && fsync(fd)) result = -errno;
  close(fd);
  if (!result && durable && (renamed || !existed)) result = tc002_sync_parent(path);
  return result;
}

int loader_ensure_directory(const char* path, int mode) {
  if (!mkdir(path, (mode_t)mode)) return tc002_sync_parent(path);
  struct stat info;
  if (errno == EEXIST && !stat(path, &info) && S_ISDIR(info.st_mode)) return 0;
  return -errno;
}
