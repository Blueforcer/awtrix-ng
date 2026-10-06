#pragma once

#include "deploy_token.h"
#include "posix_files.h"

// no_follow requires a regular token at the named path; otherwise stat/open follow links.
static inline int deploy_token_file_active(const char* token, struct deploy_token_watch* watch,
                                           double stale, int no_follow) {
  struct stat info;
  if (no_follow ? lstat(token, &info) : stat(token, &info)) return 0;
  if (no_follow && !S_ISREG(info.st_mode)) return 0;
  const double wall = tc002_clock_seconds(CLOCK_REALTIME);
  const double now = tc002_clock_seconds(CLOCK_MONOTONIC);
  if (wall < 0 || now < 0) return 0;
  const double written = (double)info.st_mtim.tv_sec + (double)info.st_mtim.tv_nsec / 1e9;
  double age = wall - written;
  if (!watch->seen) {
    char text[AWTRIX_DEPLOY_TOKEN_TEXT_LIMIT] = "";
    const int fd = open(token, O_RDONLY | O_NONBLOCK | O_CLOEXEC | (no_follow ? O_NOFOLLOW : 0));
    if (fd >= 0) {
      ssize_t count;
      do count = read(fd, text, sizeof text - 1);
      while (count < 0 && errno == EINTR);
      close(fd);
      text[count > 0 ? count : 0] = '\0';
    }
    const double uptime = tc002_clock_seconds(CLOCK_BOOTTIME);
    if (uptime < 0) return 0;
    age = deploy_token_age(text, uptime, age);
  }
  return deploy_token_fresh(watch, (long long)info.st_mtim.tv_sec, (long)info.st_mtim.tv_nsec,
                            now, age, stale);
}
