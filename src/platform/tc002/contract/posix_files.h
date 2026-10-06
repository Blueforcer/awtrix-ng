#pragma once

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static inline double tc002_clock_seconds(clockid_t clock) {
  struct timespec now;
  if (clock_gettime(clock, &now)) return -1;
  return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

/* A directory on a different device than its parent. */
static inline int tc002_mounted(const char* directory) {
  char parent[PATH_MAX];
  struct stat self, above;
  if (snprintf(parent, sizeof parent, "%s/..", directory) >= (int)sizeof parent) return 0;
  return !stat(directory, &self) && !stat(parent, &above) && self.st_dev != above.st_dev;
}

/* Sync the directory entry after a rename, creation or removal; returns -errno on failure. */
static inline int tc002_sync_parent(const char* path) {
  char copy[PATH_MAX];
  if (!path || !*path) return -EINVAL;
  if (snprintf(copy, sizeof copy, "%s", path) >= (int)sizeof copy) return -ENAMETOOLONG;
  const int fd = open(dirname(copy), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) return -errno;
  const int result = fsync(fd) ? -errno : 0;
  close(fd);
  return result;
}
