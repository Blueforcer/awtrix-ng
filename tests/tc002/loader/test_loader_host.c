#include "../../support.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "platform/posix/sha256.h"
#include "platform/tc002/contract/release_slot.h"

#ifndef AWTRIX_LOADER_TEST_ROOT
#error AWTRIX_LOADER_TEST_ROOT must name the harness directory
#endif

#define ROOT AWTRIX_LOADER_TEST_ROOT
#define DATA ROOT "/data/awtrix-ng"
#define IMAGE ROOT "/image"
#define MOUNT ROOT "/tmp/awtrix-release"
#define PROC_MTD ROOT "/proc-mtd"
#define RES ROOT "/mtd/mtd3ro"
#define RES_SIZE 0x10000
#define RES_ERASE 0x1000
#define IMAGE_BYTES 12000
#define USB_DIR ROOT "/sys/bus/platform/devices/soc:usbotg"
#define OTG_ROLE USB_DIR "/otg_role"
#define SHELL ROOT "/bin/sh"
#define RESCUE_MARKER ROOT "/tmp/awtrix-loader.rescue"
#define VENDOR_MARKER ROOT "/tmp/awtrix-loader.vendor"
#define RUNTIME_LOG ROOT "/tmp/awtrix-loader.log"
#define WORKSPACE_FD 40
#define LEAKED_FD 41
#ifndef AWTRIX_LOADER_USB_CHECK_MS
#error AWTRIX_LOADER_USB_CHECK_MS must match the loader under test
#endif
#ifndef AWTRIX_LOADER_USB_RETRY_TICKS
#error AWTRIX_LOADER_USB_RETRY_TICKS must match the loader under test
#endif
#ifndef AWTRIX_LOADER_DEPLOY_STALE_SECONDS
#error AWTRIX_LOADER_DEPLOY_STALE_SECONDS must match the loader under test
#endif
#define TICK_MS AWTRIX_LOADER_USB_CHECK_MS
#define STALE_MS ((int)(AWTRIX_LOADER_DEPLOY_STALE_SECONDS * 1000))
#define DEPLOY_DIR ROOT "/tmp/awtrix-install"
#define DEPLOY_TOKEN DEPLOY_DIR "/lock.token"


static const char* scenario;

static long long now_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

#define CHECK(condition) AWTRIX_TEST_CHECK(condition, scenario)

static char* read_file(const char* path) {
  FILE* file = fopen(path, "r");
  if (!file) return NULL;
  static char buffer[8192];
  const size_t count = fread(buffer, 1, sizeof buffer - 1, file);
  fclose(file);
  buffer[count] = '\0';
  return buffer;
}

static int contains(const char* path, const char* needle) {
  const char* text = read_file(path);
  return text && strstr(text, needle) != NULL;
}

static void write_text(const char* path, const char* text, int mode) {
  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (fd < 0 || write(fd, text, strlen(text)) < 0) {
    perror(path);
    exit(2);
  }
  close(fd);
  chmod(path, (mode_t)mode);
}

static void copy_file(const char* from, const char* to, int mode) {
  FILE* in = fopen(from, "rb");
  FILE* out = fopen(to, "wb");
  if (!in || !out) {
    perror(from);
    exit(2);
  }
  char buffer[65536];
  size_t count;
  while ((count = fread(buffer, 1, sizeof buffer, in))) fwrite(buffer, 1, count, out);
  fclose(in);
  fclose(out);
  chmod(to, (mode_t)mode);
}

static int remove_entry(const char* path, const struct stat* info, int type, struct FTW* walk) {
  (void)info;
  (void)type;
  (void)walk;
  return remove(path);
}

static void make_tree(void) {
  nftw(ROOT, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
  const char* directories[] = {ROOT,
                               ROOT "/data",
                               DATA,
                               IMAGE,
                               IMAGE "/bin",
                               ROOT "/mtd",
                               ROOT "/tmp",
                               ROOT "/res",
                               ROOT "/res/lib",
                               ROOT "/input",
                               ROOT "/storage",
                               ROOT "/storage/zkimg",
                               ROOT "/bin",
                               ROOT "/sys",
                               ROOT "/sys/bus",
                               ROOT "/sys/bus/platform",
                               ROOT "/sys/bus/platform/devices",
                               USB_DIR};
  for (size_t i = 0; i < sizeof directories / sizeof directories[0]; ++i)
    if (mkdir(directories[i], 0755)) {
      perror(directories[i]);
      exit(2);
    }
  write_text(ROOT "/props", "", 0644);
  write_text(ROOT "/workspace", "property-area", 0644);
  write_text(OTG_ROLE, "usb_device\n", 0644);
  write_text(USB_DIR "/usb_device", "acts on read\n", 0644);
  write_text(USB_DIR "/usb_host", "acts on read\n", 0644);
  write_text(USB_DIR "/usb_null", "acts on read\n", 0644);
  if (symlink(access("/bin/mksh", X_OK) ? "/bin/bash" : "/bin/mksh", SHELL)) perror("symlink");
  write_text(PROC_MTD,
             "dev:    size   erasesize  name\n"
             "mtd0: 00040000 00010000 \"boot\"\n"
             "mtd3: 00010000 00001000 \"res\"\n"
             "mtd6: 00800000 00010000 \"data\"\n",
             0644);
  /* res as the loader finds it without a release: a squashfs of one erase block, then erased
   * flash. */
  static unsigned char res[RES_SIZE];
  memset(res, 0xff, sizeof res);
  memset(res, 0, RES_ERASE);
  memcpy(res, "hsqs", 4);
  res[40] = RES_ERASE & 0xff;
  res[41] = RES_ERASE >> 8;
  const int fd = open(RES, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0 || write(fd, res, sizeof res) != (ssize_t)sizeof res) {
    perror(RES);
    exit(2);
  }
  close(fd);
}

static void reboot_tmp(void) {
  nftw(ROOT "/tmp", remove_entry, 16, FTW_DEPTH | FTW_PHYS);
  mkdir(ROOT "/tmp", 0755);
}

static const char* const* paths;

static void install_vendor(void) { copy_file(paths[1], ROOT "/res/lib/libzkgui.so", 0755); }

static void write_res(off_t offset, const void* bytes, size_t length) {
  const int fd = open(RES, O_WRONLY);
  if (fd < 0 || pwrite(fd, bytes, length, offset) != (ssize_t)length) {
    perror(RES);
    exit(2);
  }
  close(fd);
}

/* A release in the slot of res: its image stands for the tree in IMAGE, which the loader under
 * test links where it would mount the image. */
static void install_release(int mode) {
  copy_file(paths[2], IMAGE "/bin/awtrix-tc002d", mode);
  static unsigned char image[IMAGE_BYTES];
  for (size_t i = 0; i < sizeof image; ++i) image[i] = (unsigned char)(i * 7 + 3);
  struct release_slot_header header;
  memset(&header, 0, sizeof header);
  header.image_bytes = sizeof image;
  header.counter = 1790000000;
  strcpy(header.release, "1.2.0-g1");
  struct sha256_state state;
  sha256_init(&state);
  sha256_update(&state, image, sizeof image);
  sha256_final(&state, header.image_sha256);
  unsigned char encoded[RELEASE_SLOT_HEADER_BYTES];
  if (release_slot_encode(&header, encoded)) exit(2);
  write_res(2 * RES_ERASE, image, sizeof image);
  write_res(RES_ERASE, encoded, sizeof encoded);
}

static void damage_release(void) { write_res(2 * RES_ERASE + IMAGE_BYTES / 2, "x", 1); }

static void release_tree(void) {
  make_tree();
  install_vendor();
  install_release(0755);
}

static void last_property(const char* name, char* value) {
  value[0] = '\0';
  FILE* file = fopen(ROOT "/props", "r");
  if (!file) return;
  char line[256];
  const size_t length = strlen(name);
  while (fgets(line, sizeof line, file)) {
    line[strcspn(line, "\n")] = '\0';
    if (!strncmp(line, name, length) && line[length] == '=') {
      strncpy(value, line + length + 1, 91);
      value[91] = '\0';
    }
  }
  fclose(file);
}

__attribute__((visibility("default"))) int __system_property_get(const char* name, char* value) {
  last_property(name, value);
  return (int)strlen(value);
}

__attribute__((visibility("default"))) int __system_property_set(const char* name,
                                                                 const char* value) {
  FILE* file = fopen(ROOT "/props", "a");
  if (!file) return -1;
  fprintf(file, "%s=%s\n", name, value);
  fclose(file);
  return 0;
}

static void child(int close_stdin, int with_workspace) {
  const int workspace = open(ROOT "/workspace", O_RDONLY | O_CLOEXEC);
  dup3(workspace, WORKSPACE_FD, O_CLOEXEC);
  close(workspace);
  const int leaked = open(ROOT "/props", O_RDONLY);
  dup2(leaked, LEAKED_FD);
  close(leaked);
  int pipe_fds[2];
  if (pipe(pipe_fds)) _exit(3);
  if (with_workspace) {
    char value[32];
    struct stat info;
    fstat(WORKSPACE_FD, &info);
    snprintf(value, sizeof value, "%d,%ld", WORKSPACE_FD, (long)info.st_size);
    setenv("ANDROID_PROPERTY_WORKSPACE", value, 1);
  } else {
    unsetenv("ANDROID_PROPERTY_WORKSPACE");
  }
  if (close_stdin) close(0);
  signal(SIGPIPE, SIG_IGN);
  sigset_t usr1;
  sigemptyset(&usr1);
  sigaddset(&usr1, SIGUSR1);
  sigprocmask(SIG_BLOCK, &usr1, NULL);
  void* loader = dlopen(paths[0], RTLD_NOW);
  if (!loader) {
    fprintf(stderr, "%s\n", dlerror());
    _exit(4);
  }
  void (*init)(void*) = (void (*)(void*))dlsym(loader, "onEasyUIInit");
  void (*deinit)(void*) = (void (*)(void*))dlsym(loader, "onEasyUIDeinit");
  const char* (*startup)(void*) = (const char* (*)(void*))dlsym(loader, "onStartupApp");
  if (!init || !deinit || !startup) _exit(5);
  static int context;
  init(&context);
  const char* activity = startup(&context);
  deinit(&context);
  struct sigaction pipe_action;
  sigaction(SIGPIPE, NULL, &pipe_action);
  sigset_t blocked;
  sigprocmask(SIG_SETMASK, NULL, &blocked);
  FILE* out = fopen(ROOT "/harness.out", "w");
  fprintf(out, "returned %s context %p\n", activity, (void*)&context);
  fprintf(out, "sigpipe %s\n", pipe_action.sa_handler == SIG_IGN ? "ignored" : "default");
  fprintf(out, "sigusr1 %s\n", sigismember(&blocked, SIGUSR1) ? "blocked" : "open");
  fclose(out);
  _exit(0);
}

static struct {
  int watch;
  int role_writes;
  int other_access;
} usb;

static void watch_usb(void) {
  usb.role_writes = 0;
  usb.other_access = 0;
  usb.watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (usb.watch < 0 ||
      inotify_add_watch(usb.watch, USB_DIR, IN_OPEN | IN_ACCESS | IN_CLOSE_WRITE) < 0) {
    perror("inotify " USB_DIR);
    exit(2);
  }
}

static void collect_usb(void) {
  char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
  ssize_t count;
  while ((count = read(usb.watch, buffer, sizeof buffer)) > 0) {
    for (const char* at = buffer; at < buffer + count;) {
      const struct inotify_event* event = (const struct inotify_event*)at;
      if (!event->len || strcmp(event->name, "otg_role"))
        ++usb.other_access;
      else if (event->mask & IN_CLOSE_WRITE)
        ++usb.role_writes;
      at += sizeof *event + event->len;
    }
  }
  close(usb.watch);
  CHECK(usb.other_access == 0);
}

static void set_role_atomically(const char* role) {
  write_text(ROOT "/otg_role.next", role, 0644);
  if (rename(ROOT "/otg_role.next", OTG_ROLE)) perror("rename");
}

static int role_restored_within(int limit_ms) {
  for (int waited = 0; waited < limit_ms; waited += 50) {
    if (contains(OTG_ROLE, "usb_device")) return 1;
    usleep(50000);
  }
  return contains(OTG_ROLE, "usb_device");
}

static pid_t start(int close_stdin, int with_workspace) {
  unlink(ROOT "/harness.out");
  unlink(ROOT "/daemon.out");
  unlink(ROOT "/vendor.out");
  fflush(NULL);
  watch_usb();
  const pid_t pid = fork();
  if (pid == 0) child(close_stdin, with_workspace);
  return pid;
}

static void run(int close_stdin, int with_workspace) {
  const pid_t pid = start(close_stdin, with_workspace);
  int status = 0;
  waitpid(pid, &status, 0);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  collect_usb();
}

enum idle_place { IDLE_IN_SHELL, IDLE_IN_ZKGUI };

static char idle_status[4096];
static char idle_command[256];

static int exists(const char* path) { return access(path, F_OK) == 0; }

static int runs_image(pid_t pid, const char* image) {
  char link[64], target[PATH_MAX];
  snprintf(link, sizeof link, "/proc/%d/exe", (int)pid);
  const ssize_t length = readlink(link, target, sizeof target - 1);
  if (length <= 0) return 0;
  target[length] = '\0';
  return !strcmp(target, image);
}

static void snapshot(pid_t pid, const char* name, char* buffer, size_t size) {
  char path[64];
  snprintf(path, sizeof path, "/proc/%d/%s", (int)pid, name);
  buffer[0] = '\0';
  const int fd = open(path, O_RDONLY);
  if (fd < 0) return;
  const ssize_t count = read(fd, buffer, size - 1);
  close(fd);
  buffer[count > 0 ? count : 0] = '\0';
}

static struct {
  int unknown_left, host_restored, held_back, retried;
} rescue_usb;

static void exercise_rescue_usb(void) {
  memset(&rescue_usb, 0, sizeof rescue_usb);
  set_role_atomically("unkown\n");
  usleep((useconds_t)(TICK_MS * 5 / 2) * 1000);
  rescue_usb.unknown_left = contains(OTG_ROLE, "unkown");
  set_role_atomically("usb_host\n");
  rescue_usb.host_restored = role_restored_within(2 * TICK_MS);
  set_role_atomically("usb_host\n");
  usleep((useconds_t)(TICK_MS * 3 / 2) * 1000);
  rescue_usb.held_back = contains(OTG_ROLE, "usb_host");
  rescue_usb.retried = role_restored_within((AWTRIX_LOADER_USB_RETRY_TICKS + 1) * TICK_MS);
}

static void run_rescue_exercising(enum idle_place place, void (*exercise)(void)) {
  char image[PATH_MAX] = "";
  if (!realpath(place == IDLE_IN_SHELL ? SHELL : "/proc/self/exe", image)) perror("realpath");
  const pid_t pid = start(0, 1);
  int status = 0, exited = 0, idle = 0;
  for (int waited = 0; waited < 6000 && !exited && !idle; waited += 20) {
    usleep(20000);
    exited = waitpid(pid, &status, WNOHANG) == pid;
    idle = exists(RESCUE_MARKER) &&
           (place == IDLE_IN_SHELL ? runs_image(pid, image) : contains(RUNTIME_LOG, "cannot exec"));
  }
  CHECK(idle);
  CHECK(!exited);
  CHECK(runs_image(pid, image));
  if (exercise) exercise();
  snapshot(pid, "status", idle_status, sizeof idle_status);
  snapshot(pid, "cmdline", idle_command, sizeof idle_command);
  if (!exited) {
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
  }
  collect_usb();
}

static void run_rescue(enum idle_place place) { run_rescue_exercising(place, NULL); }

static void expect_rescue_usb_rules(void) {
  CHECK(rescue_usb.unknown_left);
  CHECK(rescue_usb.host_restored);
  CHECK(rescue_usb.held_back);
  CHECK(rescue_usb.retried);
}

static unsigned long long status_mask(const char* field) {
  const char* line = strstr(idle_status, field);
  return line ? strtoull(line + strlen(field), NULL, 16) : 0;
}

static int signal_bit(unsigned long long mask, int signal_number) {
  return (int)((mask >> (signal_number - 1)) & 1);
}

static int attempts(void) {
  const char* text = read_file(DATA "/state/boot-attempts");
  return text ? atoi(text) : -1;
}

static int tmp_attempts(void) {
  const char* text = read_file(ROOT "/tmp/awtrix-loader.attempts");
  return text ? atoi(text) : -1;
}

static void expect_daemon(int expected_attempts) {
  char mounted[PATH_MAX], image[PATH_MAX];
  CHECK(realpath(MOUNT, mounted) != NULL && realpath(IMAGE, image) != NULL && !strcmp(mounted, image));
  CHECK(!exists(ROOT "/harness.out"));
  CHECK(exists(ROOT "/daemon.out"));
  CHECK(contains(ROOT "/daemon.out", "arg " MOUNT "/bin/awtrix-tc002d\narg --root\narg " MOUNT "\n"));
  CHECK(contains(ROOT "/daemon.out", "arg --data\narg " DATA "\narg --boot\n"));
  CHECK(attempts() == expected_attempts);
  CHECK(contains(ROOT "/props", "sys.zkapp.state=running"));
  CHECK(contains(DATA "/state/loader.log", "exec "));
  CHECK(contains(DATA "/state/loader.log", "of release 1.2.0-g1"));
  CHECK(contains(RUNTIME_LOG, "release=valid 1.2.0-g1"));
  CHECK(contains(RUNTIME_LOG, "starting awtrix-tc002d"));
  CHECK(!exists(VENDOR_MARKER));
  CHECK(!exists(RESCUE_MARKER));
}

static void expect_vendor(const char* reason, int running) {
  CHECK(!exists(ROOT "/daemon.out"));
  CHECK(contains(ROOT "/harness.out", "returned vendorActivity"));
  CHECK(contains(ROOT "/vendor.out", "constructed"));
  CHECK(contains(ROOT "/vendor.out", "init 0x"));
  CHECK(contains(ROOT "/vendor.out", "deinit 0x"));
  CHECK(contains(RUNTIME_LOG, reason));
  CHECK(contains(ROOT "/props", "sys.zkapp.state=running") == running);
  CHECK(!exists(RESCUE_MARKER));
}

static void expect_rescue(const char* reason) {
  CHECK(!exists(ROOT "/harness.out"));
  CHECK(!exists(ROOT "/daemon.out"));
  CHECK(!exists(ROOT "/vendor.out"));
  CHECK(!exists(VENDOR_MARKER));
  CHECK(contains(ROOT "/props", "sys.zkapp.state=running"));
  CHECK(contains(RESCUE_MARKER, reason));
  CHECK(contains(RUNTIME_LOG, "rescue"));
  CHECK(contains(RUNTIME_LOG, reason));
  CHECK(contains(RUNTIME_LOG, "cannot show the rescue message: cannot export the panel latch"));
}

static void test_starts_daemon(void) {
  scenario = "start";
  release_tree();
  const long long started = now_ms();
  run(1, 1);
  CHECK(now_ms() - started >= AWTRIX_LOADER_SETTLE_MS - 1000 / sysconf(_SC_CLK_TCK));
  expect_daemon(1);
  CHECK(usb.role_writes == 0);
  const char* text = read_file(ROOT "/daemon.out");
  CHECK(text && strstr(text, "fd 0\nfd 1\nfd 2\nfd 40\n") && !strstr(text, "fd 41"));
  CHECK(contains(ROOT "/daemon.out", "workspace readable"));
  CHECK(contains(ROOT "/daemon.out", "sigpipe default\nsigusr1 open\n"));
  CHECK(!exists(ROOT "/vendor.out"));

  scenario = "start-second-attempt";
  run(0, 0);
  expect_daemon(2);
  text = read_file(ROOT "/daemon.out");
  CHECK(text && strstr(text, "fd 0\nfd 1\nfd 2\n") && !strstr(text, "fd 40") &&
        !strstr(text, "fd 41"));
}

static void test_usb_role(void) {
  scenario = "usb-role-host";
  release_tree();
  write_text(OTG_ROLE, "usb_host\n", 0644);
  run(0, 1);
  expect_daemon(1);
  CHECK(usb.role_writes == 1);
  CHECK(contains(OTG_ROLE, "usb_device"));
  CHECK(contains(RUNTIME_LOG, "usb_host"));

  scenario = "usb-role-device";
  run(0, 1);
  expect_daemon(2);
  CHECK(usb.role_writes == 0);

  scenario = "usb-role-unknown";
  release_tree();
  write_text(OTG_ROLE, "unkown\n", 0644);
  run(0, 1);
  expect_daemon(1);
  CHECK(usb.role_writes == 0);
  CHECK(contains(OTG_ROLE, "unkown"));
  CHECK(contains(RUNTIME_LOG, "USB role unkown: left as it is"));

  scenario = "rescue-keeps-usb";
  release_tree();
  mkdir(DATA "/state", 0755);
  write_text(DATA "/state/boot-attempts", "3\n", 0644);
  run_rescue_exercising(IDLE_IN_SHELL, exercise_rescue_usb);
  expect_rescue("never became healthy");
  expect_rescue_usb_rules();
  CHECK(usb.role_writes == 2);

  scenario = "rescue-in-zkgui-keeps-usb";
  unlink(SHELL);
  write_text(DATA "/state/boot-attempts", "3\n", 0644);
  write_text(OTG_ROLE, "usb_device\n", 0644);
  run_rescue_exercising(IDLE_IN_ZKGUI, exercise_rescue_usb);
  expect_rescue("never became healthy");
  expect_rescue_usb_rules();
  CHECK(usb.role_writes == 2);

  scenario = "usb-role-missing";
  release_tree();
  unlink(OTG_ROLE);
  run(0, 1);
  expect_daemon(1);
  CHECK(!exists(OTG_ROLE));
  CHECK(contains(RUNTIME_LOG, "otg_role"));
}

static void test_unhealthy_enters_rescue(void) {
  scenario = "unhealthy";
  release_tree();
  mkdir(DATA "/state", 0755);
  write_text(DATA "/state/boot-attempts", "3\n", 0644);
  write_text(OTG_ROLE, "usb_host\n", 0644);
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("never became healthy");
  CHECK(usb.role_writes == 1);
  CHECK(!memcmp(idle_command, "awtrix-loader-rescue", sizeof "awtrix-loader-rescue"));
  CHECK(signal_bit(status_mask("SigBlk:"), SIGUSR1) == 0);
  CHECK(attempts() == -1);
  CHECK(contains(DATA "/state/loader.log", "never became healthy"));

  scenario = "unhealthy-next-boot-tries-again";
  reboot_tmp();
  run(0, 1);
  expect_daemon(1);

  scenario = "unreadable-counter";
  write_text(DATA "/state/boot-attempts", "garbage", 0644);
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("never became healthy");
  CHECK(attempts() == -1);
}

static void test_vendor_sticks_this_boot(void) {
  scenario = "vendor-this-boot";
  release_tree();
  write_text(VENDOR_MARKER, "knob held at power-on: vendor app", 0644);
  run(0, 1);
  expect_vendor("already chosen in this boot", 1);
  CHECK(attempts() == -1);

  scenario = "vendor-next-boot";
  reboot_tmp();
  run(0, 1);
  expect_daemon(1);
}

static void test_unwritable_data(void) {
  scenario = "data-full";
  release_tree();
  mkdir(DATA "/state", 0755);
  mkdir(DATA "/state/boot-attempts.new", 0755);
  for (int start = 1; start <= 3; ++start) {
    run(0, 1);
    expect_daemon(-1);
    CHECK(tmp_attempts() == start);
  }
  CHECK(contains(RUNTIME_LOG, "counting starts until the next boot"));

  scenario = "data-full-unhealthy";
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("never became healthy");
  CHECK(tmp_attempts() == -1);

  scenario = "data-full-next-boot";
  reboot_tmp();
  run(0, 1);
  expect_daemon(-1);
  CHECK(tmp_attempts() == 1);

  scenario = "data-writable-again";
  rmdir(DATA "/state/boot-attempts.new");
  run(0, 1);
  expect_daemon(2);
  CHECK(tmp_attempts() == -1);

  scenario = "nowhere-to-count";
  release_tree();
  mkdir(DATA "/state", 0755);
  mkdir(DATA "/state/boot-attempts.new", 0755);
  mkdir(ROOT "/tmp/awtrix-loader.attempts.new", 0755);
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("cannot count the start");
  CHECK(attempts() == -1);
  CHECK(tmp_attempts() == -1);
}

static void test_missing_release(void) {
  scenario = "no-release";
  make_tree();
  install_vendor();
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("no release header; no AWTRIX release in res: rescue");
  CHECK(attempts() == -1);
  CHECK(!exists(MOUNT));
  CHECK(contains(DATA "/state/loader.log", "no AWTRIX release in res"));

  scenario = "no-res-partition";
  make_tree();
  install_vendor();
  install_release(0755);
  write_text(PROC_MTD, "dev:    size   erasesize  name\nmtd0: 00040000 00010000 \"boot\"\n", 0644);
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("no res partition");
  CHECK(attempts() == -1);

  scenario = "damaged-release";
  release_tree();
  damage_release();
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("the image differs from its header; the AWTRIX release in res is damaged: rescue");
  CHECK(attempts() == -1);
  CHECK(!exists(MOUNT));

  scenario = "damaged-release-stays-damaged";
  write_text(DATA "/state/boot-attempts", "1\n", 0644);
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("is damaged");
  CHECK(attempts() == 1);

  scenario = "not-executable";
  make_tree();
  install_vendor();
  install_release(0644);
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("not runnable");
  CHECK(attempts() == 1);

  scenario = "mount-failure";
  release_tree();
  mkdir(MOUNT, 0755);
  write_text(MOUNT "/occupied", "x", 0644);
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("the AWTRIX release cannot be mounted: rescue");
  CHECK(attempts() == 1);
}

static void test_exec_failure(void) {
  scenario = "exec-failure";
  release_tree();
  write_text(IMAGE "/bin/awtrix-tc002d", "not an executable\n", 0755);
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("exec failed");
  CHECK(attempts() == 1);

  scenario = "exec-failure-without-shell";
  unlink(SHELL);
  run_rescue(IDLE_IN_ZKGUI);
  expect_rescue("exec failed");
  CHECK(attempts() == 2);
  CHECK(signal_bit(status_mask("SigIgn:"), SIGPIPE) == 1);
  CHECK(signal_bit(status_mask("SigBlk:"), SIGUSR1) == 1);
}

static void test_pending_vendor_update(void) {
  scenario = "update-pending";
  release_tree();
  write_text(ROOT "/storage/update.img", "img", 0644);
  write_text(ROOT "/props", "sys.zkupgrade.flag=255\nsys.zkupgrade.dir=" ROOT "/storage\n", 0644);
  run(0, 1);
  expect_vendor("vendor update pending", 1);
  CHECK(attempts() == -1);
  CHECK(!exists(VENDOR_MARKER));

  scenario = "update-pending-while-unhealthy";
  mkdir(DATA "/state", 0755);
  write_text(DATA "/state/boot-attempts", "3\n", 0644);
  run(0, 1);
  expect_vendor("vendor update pending", 1);
  CHECK(attempts() == 3);

  scenario = "update-default-dir-zkimg";
  release_tree();
  write_text(ROOT "/storage/zkimg/update.img", "img", 0644);
  write_text(ROOT "/props", "sys.zkupgrade.flag=1\n", 0644);
  run(0, 1);
  expect_vendor("vendor update pending", 1);

  scenario = "update-flag-without-image";
  release_tree();
  write_text(ROOT "/props", "sys.zkupgrade.flag=255\n", 0644);
  run(0, 1);
  expect_daemon(1);

  scenario = "update-flag-consumed";
  release_tree();
  write_text(ROOT "/storage/update.img", "img", 0644);
  write_text(ROOT "/props", "sys.zkupgrade.flag=0\n", 0644);
  run(0, 1);
  expect_daemon(1);
}

static void test_vendor_library_missing(void) {
  scenario = "no-vendor-no-release";
  make_tree();
  nftw(DATA, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
  write_text(OTG_ROLE, "usb_null\n", 0644);
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("no AWTRIX release in res");
  CHECK(!exists(DATA));
  CHECK(usb.role_writes == 1);
  CHECK(contains(OTG_ROLE, "usb_device"));

  scenario = "vendor-missing-for-update";
  make_tree();
  install_release(0755);
  write_text(ROOT "/storage/update.img", "img", 0644);
  write_text(ROOT "/props", "sys.zkupgrade.flag=255\nsys.zkupgrade.dir=" ROOT "/storage\n", 0644);
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("vendor app cannot be loaded");
  CHECK(contains(RUNTIME_LOG, "cannot load"));
  CHECK(contains(DATA "/state/loader.log", "vendor app cannot be loaded"));
  CHECK(attempts() == -1);

  scenario = "vendor-missing-this-boot";
  make_tree();
  install_release(0755);
  write_text(VENDOR_MARKER, "knob held at power-on: vendor app", 0644);
  run_rescue(IDLE_IN_SHELL);
  CHECK(!exists(ROOT "/daemon.out"));
  CHECK(contains(ROOT "/props", "sys.zkapp.state=running"));
  CHECK(contains(RESCUE_MARKER, "vendor app cannot be loaded"));
  CHECK(attempts() == -1);
}

static void write_token_text(const char* text, long long age_seconds) {
  mkdir(DEPLOY_DIR, 0700);
  write_text(DEPLOY_TOKEN, text, 0600);
  if (!age_seconds) return;
  struct timespec times[2];
  clock_gettime(CLOCK_REALTIME, &times[0]);
  times[0].tv_sec -= (time_t)age_seconds;
  times[1] = times[0];
  if (utimensat(AT_FDCWD, DEPLOY_TOKEN, times, 0)) perror("utimensat");
}

static void write_token(int age_seconds) { write_token_text("deploy\n", age_seconds); }

static void write_token_uptime(double uptime_age, long long mtime_age) {
  struct timespec now;
  clock_gettime(CLOCK_BOOTTIME, &now);
  double written = (double)now.tv_sec + (double)now.tv_nsec / 1e9 - uptime_age;
  /* Shortly after a boot that lies before it; a token written at boot is stale all the same. */
  if (written < 0) written = 0;
  char text[64];
  snprintf(text, sizeof text, "deploy %.2f\n", written);
  write_token_text(text, mtime_age);
}

static int finished(pid_t pid, int* status) { return waitpid(pid, status, WNOHANG) == pid; }

static int finished_within(pid_t pid, int* status, int limit_ms) {
  for (int waited = 0; waited < limit_ms; waited += 50) {
    if (finished(pid, status)) return 1;
    usleep(50000);
  }
  if (finished(pid, status)) return 1;
  kill(pid, SIGKILL);
  waitpid(pid, status, 0);
  return 0;
}

static int role_is_device(void) { return contains(OTG_ROLE, "usb_device"); }

static int held_while_refreshed(pid_t pid, int limit_ms, int (*done)(void)) {
  int status = 0;
  for (int waited = 0; waited < limit_ms; waited += 10) {
    if (waited % (STALE_MS / 3) == 0) write_token(0);
    if (finished(pid, &status)) return 0;
    if (done && done()) return 1;
    usleep(10000);
  }
  return !finished(pid, &status) && (!done || done());
}

static void test_deploy_hold(void) {
  scenario = "deploy-hold-until-token-removed";
  make_tree();
  install_vendor();
  write_token(0);
  pid_t pid = start(0, 1);
  int status = 0, logged = 0;
  for (int waited = 0; waited < 3000 && !logged; waited += 50) {
    logged = contains(RUNTIME_LOG, "USB deploy in progress: waiting for the installer");
    usleep(50000);
  }
  CHECK(logged);
  CHECK(held_while_refreshed(pid, STALE_MS * 5 / 3, NULL));
  CHECK(!exists(ROOT "/daemon.out"));
  CHECK(!exists(ROOT "/harness.out"));
  CHECK(!exists(ROOT "/vendor.out"));
  CHECK(!exists(RESCUE_MARKER));
  CHECK(contains(ROOT "/props", "sys.zkapp.state=running"));
  CHECK(attempts() == -1);
  set_role_atomically("usb_host\n");
  CHECK(held_while_refreshed(pid, 3 * TICK_MS, role_is_device));
  install_release(0755);
  unlink(DEPLOY_TOKEN);
  CHECK(finished_within(pid, &status, 4000));
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  collect_usb();
  CHECK(usb.role_writes == 1);
  expect_daemon(1);
  CHECK(contains(RUNTIME_LOG, "otg_role usb_host, selected usb_device"));
  CHECK(contains(RUNTIME_LOG, "deploy token removed: deciding again"));
  CHECK(contains(DATA "/state/loader.log", "USB deploy in progress"));

  scenario = "deploy-hold-ends-when-the-token-goes-stale";
  release_tree();
  write_token(0);
  const long long started = now_ms();
  pid = start(0, 1);
  CHECK(finished_within(pid, &status, STALE_MS + 4000));
  const long long held = now_ms() - started;
  CHECK(held >= STALE_MS - AWTRIX_LOADER_DEPLOY_CHECK_MS);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  collect_usb();
  expect_daemon(1);
  CHECK(contains(RUNTIME_LOG, "deploy token went stale: deciding again"));

  scenario = "deploy-hold-goes-stale-into-rescue";
  make_tree();
  install_vendor();
  write_token(0);
  run_rescue(IDLE_IN_SHELL);
  expect_rescue("no AWTRIX release in res");
  CHECK(contains(RUNTIME_LOG, "deploy token went stale: deciding again"));

  scenario = "stale-deploy-token-ignored";
  release_tree();
  write_token(AWTRIX_LOADER_DEPLOY_STALE_SECONDS + 60);
  run(0, 1);
  expect_daemon(1);
  CHECK(!contains(RUNTIME_LOG, "USB deploy"));

  scenario = "long-stale-deploy-token-ignored";
  release_tree();
  write_token(3600);
  run(0, 1);
  expect_daemon(1);
  CHECK(!contains(RUNTIME_LOG, "USB deploy"));

  scenario = "deploy-hold-follows-the-uptime-in-the-token-not-a-stepped-clock";
  release_tree();
  write_token_uptime(AWTRIX_LOADER_DEPLOY_STALE_SECONDS / 4, 56LL * 365 * 86400);
  pid = start(0, 1);
  logged = 0;
  for (int waited = 0; waited < 3000 && !logged; waited += 50) {
    logged = contains(RUNTIME_LOG, "USB deploy in progress: waiting for the installer");
    usleep(50000);
  }
  CHECK(logged);
  CHECK(!finished(pid, &status));
  unlink(DEPLOY_TOKEN);
  CHECK(finished_within(pid, &status, 4000));
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  collect_usb();
  expect_daemon(1);
  CHECK(contains(RUNTIME_LOG, "deploy token removed: deciding again"));

  scenario = "deploy-token-stale-by-its-uptime-ignored-whatever-its-mtime";
  release_tree();
  write_token_uptime(AWTRIX_LOADER_DEPLOY_STALE_SECONDS + 60, -3600);
  run(0, 1);
  expect_daemon(1);
  CHECK(!contains(RUNTIME_LOG, "USB deploy"));

  scenario = "vendor-this-boot-before-deploy-hold";
  release_tree();
  write_text(VENDOR_MARKER, "knob held at power-on: vendor app", 0644);
  write_token(0);
  run(0, 1);
  expect_vendor("already chosen in this boot", 1);
  CHECK(!contains(RUNTIME_LOG, "USB deploy"));

  scenario = "vendor-update-before-deploy-hold";
  release_tree();
  write_text(ROOT "/storage/update.img", "img", 0644);
  write_text(ROOT "/props", "sys.zkupgrade.flag=255\nsys.zkupgrade.dir=" ROOT "/storage\n", 0644);
  write_token(0);
  run(0, 1);
  expect_vendor("vendor update pending", 1);
  CHECK(!contains(RUNTIME_LOG, "USB deploy"));
}

int main(int argc, char** argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s LOADER_SO VENDOR_SO DAEMON\n", argv[0]);
    return 2;
  }
  paths = (const char* const*)argv + 1;
  test_starts_daemon();
  test_usb_role();
  test_unhealthy_enters_rescue();
  test_vendor_sticks_this_boot();
  test_unwritable_data();
  test_missing_release();
  test_exec_failure();
  test_pending_vendor_update();
  test_vendor_library_missing();
  test_deploy_hold();
  nftw(ROOT, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
  if (awtrix_test_failures) {
    fprintf(stderr, "%d check(s) failed\n", awtrix_test_failures);
    return 1;
  }
  puts("loader host tests passed");
  return 0;
}
