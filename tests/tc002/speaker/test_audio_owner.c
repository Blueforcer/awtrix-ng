/* Runs the helper's stock GUI scan over a private synthetic proc tree with errors injected into
 * its metadata calls, and its file scan over private files. Nothing outside the fixture is
 * opened for writing. */
#define _GNU_SOURCE
#include "../../support.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum call { CALL_OPEN, CALL_OPENDIR, CALL_READLINK };

static struct {
  enum call call;
  char path[PATH_MAX];
  int error;
  int pending;
} fault;

static int inject(enum call call, const char *path) {
  if (!fault.pending || call != fault.call || strcmp(path, fault.path)) return 0;
  fault.pending = 0;
  errno = fault.error;
  return 1;
}

static int fault_open(const char *path, int flags) {
  if (inject(CALL_OPEN, path)) return -1;
  return open(path, flags);
}

static DIR *fault_opendir(const char *path) {
  if (inject(CALL_OPENDIR, path)) return NULL;
  return opendir(path);
}

static ssize_t fault_readlink(const char *path, char *out, size_t size) {
  if (inject(CALL_READLINK, path)) return -1;
  return readlink(path, out, size);
}

#define open(path, flags) fault_open(path, flags)
#define opendir(path) fault_opendir(path)
#define readlink(path, out, size) fault_readlink(path, out, size)
#include "platform/tc002/audio/helper/audio_owner.c"
#undef readlink
#undef opendir
#undef open

static char base[PATH_MAX], proc[PATH_MAX];

static void (*const require)(int, const char*) = awtrix_test_require;

static const char *at(const char *relative) {
  static char path[PATH_MAX];
  require(snprintf(path, sizeof path, "%s/%s", base, relative) < (int)sizeof path, "fixture path");
  return path;
}

static void directory(const char *relative) {
  require(!mkdir(at(relative), 0700) || errno == EEXIST, "fixture directory");
}

static void text(const char *relative, const char *content) {
  FILE *f = fopen(at(relative), "w");
  require(f && fputs(content, f) >= 0 && !fclose(f), "fixture file");
}

static void link_to(const char *target, const char *relative) {
  unlink(at(relative));
  require(!symlink(target, at(relative)), "fixture link");
}

/* proc/<pid>/task/<tid> with its name and executable. */
static void task(const char *pid, const char *tid, const char *comm, const char *exe) {
  char relative[256];
  snprintf(relative, sizeof relative, "proc/%s", pid);
  directory(relative);
  snprintf(relative, sizeof relative, "proc/%s/task", pid);
  directory(relative);
  snprintf(relative, sizeof relative, "proc/%s/task/%s", pid, tid);
  directory(relative);
  snprintf(relative, sizeof relative, "proc/%s/task/%s/comm", pid, tid);
  text(relative, comm);
  snprintf(relative, sizeof relative, "proc/%s/task/%s/exe", pid, tid);
  link_to(exe, relative);
}

struct fault_case {
  enum call call;
  const char *relative;
  int hidden_allowed;
};

static void test_errors(void) {
  static const struct fault_case cases[] = {
      {CALL_OPENDIR, "proc/20/task", 0},
      {CALL_OPEN, "proc/20/task/21/comm", 0},
      {CALL_READLINK, "proc/20/task/21/exe", 1},
  };
  static const int errors[] = {ENOENT, ESRCH, EACCES, EPERM, EIO, EINVAL, EBADF};
  size_t c, e;
  for (c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
    for (e = 0; e < sizeof errors / sizeof errors[0]; ++e) {
      const int error = errors[e];
      const int vanished = error == ENOENT || error == ESRCH;
      const int hidden = cases[c].hidden_allowed && (error == EACCES || error == EPERM);
      int okay;
      fault.call = cases[c].call;
      snprintf(fault.path, sizeof fault.path, "%s", at(cases[c].relative));
      fault.error = error;
      fault.pending = 1;
      okay = ah_stock_gui_absent(proc);
      require(!fault.pending, "the injected error reached its call");
      if (okay != (vanished || hidden)) {
        fprintf(stderr, "%s with %s: scan %s\n", cases[c].relative, strerror(error),
                okay ? "passed" : "refused");
        require(0, "only vanished tasks and executables hidden mid-exec are skipped");
      }
    }
  }
}

static void write_padded(const char *relative, size_t padding, const char *filler_line,
                         const char *tail) {
  FILE *f = fopen(at(relative), "w");
  size_t written = 0;
  require(f != NULL, "fixture file");
  while (written + strlen(filler_line) <= padding) written += (size_t)fprintf(f, "%s", filler_line);
  while (written < padding) written += (size_t)fprintf(f, "x");
  require(fputs(tail, f) >= 0 && !fclose(f), "fixture file");
}

static void test_file_scans(void) {
  size_t offset;
  text("interrupts", "           CPU0\n 41:        120     GIC  41 Level     aio_dma2\n");
  require(ah_file_contains(at("interrupts"), "aio_dma") == 1, "an audio DMA interrupt is found");
  text("interrupts", "           CPU0\n 30:        120     GIC  30 Level     timer\n");
  require(ah_file_contains(at("interrupts"), "aio_dma") == 0, "no audio DMA interrupt");
  require(ah_file_contains(at("missing"), "aio_dma") == -1, "unreadable interrupts are reported");
  for (offset = 1000; offset < 1060; ++offset) {
    write_padded("interrupts", offset, " 30: 1 GIC timer\n", "aio_dma2\n");
    require(ah_file_contains(at("interrupts"), "aio_dma") == 1, "text across a read boundary is found");
  }
  write_padded("interrupts", 5000, " 30: 1 GIC timer\n", "aio_dm\n");
  require(ah_file_contains(at("interrupts"), "aio_dma") == 0, "a partial match is not a match");
}

static int remove_entry(const char *path, const struct stat *st, int type, struct FTW *walk) {
  (void)st;
  (void)type;
  (void)walk;
  return remove(path);
}

int main(void) {
  char pattern[] = "/tmp/tc002-audio-owner-XXXXXX";
  const char *made = mkdtemp(pattern);
  require(made && realpath(made, base), "fixture root");
  require(snprintf(proc, sizeof proc, "%s/proc", base) < (int)sizeof proc, "fixture paths");
  directory("proc");
  directory("proc/sys");
  task("1", "1", "init\n", "/sbin/init");
  task("20", "20", "awtrix-linux\n", "/data/awtrix-ng/bin/awtrix-linux");
  task("20", "21", "render\n", "/data/awtrix-ng/bin/awtrix-linux");

  require(ah_stock_gui_absent(proc), "a tree without the stock GUI is clear");
  require(!ah_stock_gui_absent(at("none")), "an unreadable proc refuses");
  task("30", "30", "zkgui\n", "/bin/other");
  require(!ah_stock_gui_absent(proc), "the stock GUI is found by name");
  task("30", "30", "other\n", "/bin/zkgui");
  require(!ah_stock_gui_absent(proc), "the stock GUI is found by executable");
  task("30", "30", "other\n", "/bin/other");
  require(ah_stock_gui_absent(proc), "any other task is not");

  test_errors();
  test_file_scans();

  require(!nftw(base, remove_entry, 16, FTW_DEPTH | FTW_PHYS), "fixture removed");
  printf("tc002 audio owner scan: %u passed\n", awtrix_test_passed);
  return 0;
}
