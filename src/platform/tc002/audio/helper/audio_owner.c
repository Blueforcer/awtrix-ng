#define _GNU_SOURCE
#include "audio_owner.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int missing(void) { return errno == ENOENT || errno == ESRCH; }
static int hidden(void) { return errno == EACCES || errno == EPERM; }

int ah_numeric(const char *p) {
  if (!*p) return 0;
  for (; *p; ++p)
    if (*p < '0' || *p > '9') return 0;
  return 1;
}

int ah_read_small(const char *path, uint8_t *out, size_t capacity, size_t *length) {
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK), result = 0;
  *length = 0;
  if (fd < 0) return 0;
  while (*length < capacity) {
    const ssize_t n = read(fd, out + *length, capacity - *length);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) break;
    if (!n) {
      result = 1;
      break;
    }
    *length += (size_t)n;
  }
  close(fd);
  return result && *length < capacity;
}

static int task_clear(const char *root) {
  char path[192], executable[192];
  uint8_t comm[128];
  size_t count;
  ssize_t n;
  snprintf(path, sizeof path, "%s/comm", root);
  if (!ah_read_small(path, comm, sizeof comm, &count)) return missing();
  if (count >= 5 && !memcmp(comm, "zkgui", 5)) return 0;
  snprintf(path, sizeof path, "%s/exe", root);
  n = readlink(path, executable, sizeof executable);
  if (n < 0 && !missing() && !hidden()) return 0;
  if (n >= (ssize_t)sizeof executable) return 0;
  return !(n >= 10 && !memcmp(executable, "/bin/zkgui", 10));
}

int ah_stock_gui_absent(const char *proc) {
  DIR *processes;
  struct dirent *process;
  unsigned process_count = 0, task_count = 0;
  int okay = 0;
  processes = opendir(proc);
  if (!processes) return 0;
  for (;;) {
    char path[128];
    DIR *tasks;
    struct dirent *task;
    int tasks_okay = 0;
    errno = 0;
    process = readdir(processes);
    if (!process) {
      okay = errno == 0;
      break;
    }
    if (!ah_numeric(process->d_name)) continue;
    if (++process_count > 1024 || strlen(process->d_name) > 20) break;
    if (snprintf(path, sizeof path, "%s/%.20s/task", proc, process->d_name) >= (int)sizeof path)
      break;
    tasks = opendir(path);
    if (!tasks) {
      if (missing()) continue;
      break;
    }
    for (;;) {
      char root[160];
      errno = 0;
      task = readdir(tasks);
      if (!task) {
        tasks_okay = errno == 0;
        break;
      }
      if (!ah_numeric(task->d_name)) continue;
      if (++task_count > 4096 || strlen(task->d_name) > 20) break;
      snprintf(root, sizeof root, "%s/%.20s", path, task->d_name);
      if (!task_clear(root)) break;
    }
    closedir(tasks);
    if (!tasks_okay) break;
  }
  closedir(processes);
  return okay;
}

int ah_file_contains(const char *path, const char *text) {
  char window[1024];
  const size_t length = strlen(text);
  size_t filled = 0;
  int fd;
  if (!length || length > sizeof window / 2) return -1;
  fd = open(path, O_RDONLY | O_CLOEXEC | O_NOCTTY);
  if (fd < 0) return -1;
  for (;;) {
    const ssize_t n = read(fd, window + filled, sizeof window - filled);
    size_t i;
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      close(fd);
      return n < 0 ? -1 : 0;
    }
    filled += (size_t)n;
    for (i = 0; i + length <= filled; ++i)
      if (!memcmp(window + i, text, length)) {
        close(fd);
        return 1;
      }
    if (filled >= length) {
      memmove(window, window + filled - (length - 1), length - 1);
      filled = length - 1;
    }
  }
}
