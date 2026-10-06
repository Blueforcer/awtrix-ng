#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <linux/loop.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../contract/deploy_token_file.h"
#include "../flasher/mtd_table.h"
#include "../flasher/slot_io.h"
#include "loader_paths.h"
#include "loader_policy.h"
#include "loader_state.h"
#include "rescue_panel.h"

#define LOADER_EXPORT __attribute__((visibility("default")))
#define PROPERTY_VALUE_LIMIT 92
#define KNOB_PUSH_KEY KEY_UP
#define TEXT(value) #value
#define NUMBER_TEXT(value) TEXT(value)
#define RESCUE_IDLE_SCRIPT                                                                          \
  "last=; hold=0; lines=0; while read -r tick; do role=; read -r role <\"$1\" 2>/dev/null; "        \
  "[ $hold -gt 0 ] && hold=$((hold - 1)); did=; case $role in usb_host|usb_null) "                  \
  "if [ $hold -eq 0 ]; then hold=" NUMBER_TEXT(AWTRIX_LOADER_USB_RETRY_TICKS) "; "                  \
  "if echo -n " LOADER_USB_DEVICE_ROLE " >\"$1\" 2>/dev/null; then did=', selected "                \
  LOADER_USB_DEVICE_ROLE "'; else did=', cannot select " LOADER_USB_DEVICE_ROLE "'; fi; fi;; "      \
  "esac; if [ \"$role\" != \"$last\" ] || [ -n \"$did\" ]; then last=$role; "                        \
  "lines=$((lines + 1)); if [ $lines -le " NUMBER_TEXT(LOADER_RESCUE_LOG_LINES) " ]; then up=; "  \
  "read -r up rest </proc/uptime; echo \"[${up}s] otg_role ${role:-unreadable}$did\" >>\"$2\"; " \
  "fi; fi; done"

typedef void (*easyui_hook)(void*);
typedef const char* (*easyui_startup)(void*);
typedef int (*property_get_function)(const char*, char*);
typedef int (*property_set_function)(const char*, const char*);

static struct {
  void* library;
  easyui_hook init;
  easyui_hook deinit;
  easyui_startup startup;
} vendor;

static char journal[2048];
static size_t journal_used;

static double uptime_seconds(void) {
  char text[64] = "";
  const int fd = open("/proc/uptime", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return 0;
  const ssize_t count = read(fd, text, sizeof text - 1);
  close(fd);
  if (count <= 0) return 0;
  text[count] = '\0';
  return strtod(text, NULL);
}

static double process_age_seconds(void) {
  char text[512];
  const int fd = open("/proc/self/stat", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  const ssize_t count = read(fd, text, sizeof text - 1);
  close(fd);
  if (count <= 0) return -1;
  text[count] = '\0';
  const char* field = strrchr(text, ')');
  for (int index = 2; field && index < 22; ++index) field = strchr(field + 1, ' ');
  const long ticks = sysconf(_SC_CLK_TCK);
  if (!field || ticks <= 0) return -1;
  return uptime_seconds() - strtod(field + 1, NULL) / (double)ticks;
}

static void boot_tag(char tag[9]) {
  strcpy(tag, "--------");
  const int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return;
  char text[9];
  if (read(fd, text, 8) == 8) {
    memcpy(tag, text, 8);
    tag[8] = '\0';
  }
  close(fd);
}

static void note(const char* format, ...) {
  char line[512];
  char tag[9];
  boot_tag(tag);
  int used = snprintf(line, sizeof line, "[%8.2fs] %s ", uptime_seconds(), tag);
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(line + used, sizeof line - (size_t)used - 1, format, arguments);
  va_end(arguments);
  strcat(line, "\n");
  loader_append_log(LOADER_RUNTIME_LOG, line, LOADER_RUNTIME_LOG_LIMIT, 0);
  const size_t length = strlen(line);
  if (journal_used + length < sizeof journal) {
    memcpy(journal + journal_used, line, length + 1);
    journal_used += length;
  }
}

static void flush_journal(void) {
  struct stat info;
  if (!journal_used || stat(AWTRIX_LOADER_DATA_DIR, &info) || !S_ISDIR(info.st_mode)) return;
  if (!loader_ensure_directory(LOADER_STATE_DIR, 0755))
    loader_append_log(LOADER_DURABLE_LOG, journal, LOADER_DURABLE_LOG_LIMIT, 1);
  journal_used = 0;
  journal[0] = '\0';
}

static int read_usb_role(char* role, size_t size) {
  const int fd = open(AWTRIX_LOADER_USB_ROLE, O_RDONLY | O_CLOEXEC | O_NOCTTY);
  if (fd < 0) return -errno;
  ssize_t count;
  do count = read(fd, role, size - 1);
  while (count < 0 && errno == EINTR);
  const int error = errno;
  close(fd);
  if (count < 0) return -error;
  while (count > 0 && isspace((unsigned char)role[count - 1])) --count;
  role[count] = '\0';
  return 0;
}

static int write_usb_role(const char* role) {
  const int fd = open(AWTRIX_LOADER_USB_ROLE, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOCTTY);
  if (fd < 0) return -errno;
  const size_t length = strlen(role);
  ssize_t count;
  do count = write(fd, role, length);
  while (count < 0 && errno == EINTR);
  const int error = count < 0 ? -errno : (size_t)count == length ? 0 : -EIO;
  close(fd);
  return error;
}

static int usb_role_is_definite_other(const char* role) {
  return !strcmp(role, "usb_host") || !strcmp(role, "usb_null");
}

static void select_usb_device(void) {
  char role[32];
  const int unread = read_usb_role(role, sizeof role);
  if (unread) {
    note("cannot read %s: %s", AWTRIX_LOADER_USB_ROLE, strerror(-unread));
    return;
  }
  if (!usb_role_is_definite_other(role)) {
    note("USB role %s: left as it is", role);
    return;
  }
  const int unwritten = write_usb_role(LOADER_USB_DEVICE_ROLE);
  char now[32];
  note("USB role %s: %s %s%s%s, now %s", role, unwritten ? "cannot select" : "selected",
       LOADER_USB_DEVICE_ROLE, unwritten ? ": " : "", unwritten ? strerror(-unwritten) : "",
       read_usb_role(now, sizeof now) ? "unreadable" : now);
}

static void append_rescue_marker(const char* line) {
  const int fd = open(LOADER_RESCUE_MARKER, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0) return;
  if (write(fd, line, strlen(line)) < 0)
    note("cannot append to %s: %s", LOADER_RESCUE_MARKER, strerror(errno));
  close(fd);
}

static void keep_usb_device(int into_rescue_marker) {
  static char last[32] = "\n";
  static int hold, lines;
  char role[32] = "";
  if (read_usb_role(role, sizeof role)) role[0] = '\0';
  if (hold > 0) --hold;
  const char* did = "";
  if (usb_role_is_definite_other(role) && !hold) {
    hold = AWTRIX_LOADER_USB_RETRY_TICKS;
    did = write_usb_role(LOADER_USB_DEVICE_ROLE) ? ", cannot select " LOADER_USB_DEVICE_ROLE
                                                 : ", selected " LOADER_USB_DEVICE_ROLE;
  }
  if (!into_rescue_marker) {
    if (*did) note("otg_role %s%s", role, did);
    return;
  }
  if (!strcmp(role, last) && !*did) return;
  snprintf(last, sizeof last, "%s", role);
  if (++lines > LOADER_RESCUE_LOG_LINES) return;
  char line[128];
  snprintf(line, sizeof line, "[%.2fs] otg_role %s%s\n", uptime_seconds(), role[0] ? role : "unreadable",
           did);
  append_rescue_marker(line);
}

static int has_bit(const unsigned char* bits, unsigned index) {
  return (bits[index / 8] >> (index % 8)) & 1;
}

static int knob_state(void) {
  DIR* directory = opendir(AWTRIX_LOADER_INPUT_DIR);
  if (!directory) return -1;
  int state = -1;
  struct dirent* entry;
  while (state < 0 && (entry = readdir(directory))) {
    if (strncmp(entry->d_name, "event", 5)) continue;
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", AWTRIX_LOADER_INPUT_DIR, entry->d_name);
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) continue;
    char name[64] = "";
    unsigned char supported[KEY_MAX / 8 + 1] = {0};
    unsigned char pressed[KEY_MAX / 8 + 1] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) >= 0 &&
        !strcmp(name, LOADER_KEYS_DEVICE_NAME) &&
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof supported), supported) >= 0 &&
        has_bit(supported, KNOB_PUSH_KEY) && ioctl(fd, EVIOCGKEY(sizeof pressed), pressed) >= 0)
      state = has_bit(pressed, KNOB_PUSH_KEY);
    close(fd);
  }
  closedir(directory);
  return state;
}

static int property_get(const char* name, char value[PROPERTY_VALUE_LIMIT]) {
  value[0] = '\0';
  property_get_function get =
      (property_get_function)dlsym(RTLD_DEFAULT, "__system_property_get");
  if (!get) return -1;
  const int length = get(name, value);
  value[PROPERTY_VALUE_LIMIT - 1] = '\0';
  return length < 0 ? -1 : 0;
}

static int run_setprop(const char* name, const char* value) {
  const pid_t child = fork();
  if (child < 0) return -1;
  if (child == 0) {
    execl(AWTRIX_LOADER_SETPROP, "setprop", name, value, (char*)NULL);
    _exit(127);
  }
  int status = 0;
  pid_t reaped;
  do reaped = waitpid(child, &status, 0);
  while (reaped < 0 && errno == EINTR);
  if (reaped < 0) return errno == ECHILD ? 1 : -1;
  return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int property_set(const char* name, const char* value) {
  property_set_function set =
      (property_set_function)dlsym(RTLD_DEFAULT, "__system_property_set");
  int result = set ? (set(name, value) == 0 ? 0 : -1) : run_setprop(name, value);
  char check[PROPERTY_VALUE_LIMIT];
  if (!property_get(name, check)) result = strcmp(check, value) ? -1 : 0;
  return result;
}

static void announce_running(void) {
  if (property_set("sys.zkapp.state", "running")) note("could not set sys.zkapp.state=running");
}

static int regular_file(const char* path) {
  struct stat info;
  return !stat(path, &info) && S_ISREG(info.st_mode);
}

static int upgrade_pending(char* where, size_t where_size) {
  char flag[PROPERTY_VALUE_LIMIT], sys_dir[PROPERTY_VALUE_LIMIT], persist_dir[PROPERTY_VALUE_LIMIT];
  if (property_get("sys.zkupgrade.flag", flag) || !loader_upgrade_requested(flag)) return 0;
  property_get("sys.zkupgrade.dir", sys_dir);
  property_get("persist.zkupgrade.dir", persist_dir);
  const char* directory = loader_upgrade_dir(sys_dir, persist_dir, AWTRIX_LOADER_UPGRADE_DIR);
  const char* candidates[] = {"/update.img", "/zkimg/update.img"};
  for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; ++i) {
    snprintf(where, where_size, "%s%s", directory, candidates[i]);
    if (regular_file(where)) return 1;
  }
  where[0] = '\0';
  return 0;
}

/* The release slot behind the res squashfs (release_slot.h), as the loader found it. */
struct release {
  enum loader_release state;
  unsigned index;
  struct release_slot_layout layout;
  struct release_slot_header header;
  char problem[128];
};

struct counter {
  const char* durable;
  const char* this_boot;
};

static const struct counter boot_counter = {LOADER_ATTEMPTS_FILE, LOADER_RUNTIME_ATTEMPTS_FILE};

static int present(const char* path) {
  struct stat info;
  return !lstat(path, &info) || errno != ENOENT;
}

static int read_descriptor(void* context, uint64_t offset, void* buffer, size_t length) {
  const int fd = *(const int*)context;
  unsigned char* bytes = buffer;
  while (length) {
    const ssize_t count = pread(fd, bytes, length, (off_t)offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return count < 0 ? -errno : -EIO;
    bytes += count;
    offset += (uint64_t)count;
    length -= (size_t)count;
  }
  return 0;
}

static int read_small_file(const char* path, char* text, size_t size) {
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
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
  return 0;
}

/* Reads res through its read-only MTD device and checks the slot's header and image. */
static void inspect_release(struct release* release) {
  memset(release, 0, sizeof *release);
  char table[4096];
  struct mtd_entry entry;
  int result = read_small_file(AWTRIX_LOADER_PROC_MTD, table, sizeof table);
  if (result || mtd_table_find_name(table, TC002_RES_PARTITION, &entry)) {
    snprintf(release->problem, sizeof release->problem, "no %s partition in %s", TC002_RES_PARTITION,
             AWTRIX_LOADER_PROC_MTD);
    return;
  }
  release->index = entry.index;
  char path[PATH_MAX];
  snprintf(path, sizeof path, "%s/mtd%uro", AWTRIX_LOADER_MTD_DIR, entry.index);
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    snprintf(release->problem, sizeof release->problem, "cannot open mtd%uro: %s", entry.index,
             strerror(errno));
    return;
  }
  struct flash_device device;
  memset(&device, 0, sizeof device);
  device.context = &fd;
  device.size = entry.size;
  device.erase_size = entry.erase_size;
  device.read = read_descriptor;
  result = slot_read(&device, 1, &release->layout, &release->header);
  close(fd);
  if (!result) {
    release->state = LOADER_RELEASE_VALID;
  } else if (result == -EBADMSG) {
    release->state = LOADER_RELEASE_DAMAGED;
    snprintf(release->problem, sizeof release->problem, "the image differs from its header");
  } else {
    snprintf(release->problem, sizeof release->problem, "%s",
             result == -ENOENT    ? "no release header"
             : result == -ENOSPC  ? "res leaves no release slot"
             : result == -EINVAL  ? "res does not start with a squashfs"
                                  : strerror(-result));
  }
}

static void write_marker(const char* path, const char* reason) {
  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) {
    note("cannot create %s: %s", path, strerror(errno));
    return;
  }
  char line[384];
  snprintf(line, sizeof line, "%s\n", reason);
  if (write(fd, line, strlen(line)) < 0) note("cannot write %s: %s", path, strerror(errno));
  close(fd);
}

static int property_workspace_descriptor(void) {
  const char* value = getenv("ANDROID_PROPERTY_WORKSPACE");
  if (!value) return -1;
  char* end;
  const long fd = strtol(value, &end, 10);
  if (end == value || *end != ',' || fd < 3 || fd > INT_MAX) return -1;
  const char* size_text = end + 1;
  const unsigned long size = strtoul(size_text, &end, 10);
  struct stat info;
  if (end == size_text || *end || !size || fstat((int)fd, &info) || !S_ISREG(info.st_mode) ||
      (unsigned long)info.st_size < size || (fcntl((int)fd, F_GETFL) & O_ACCMODE) != O_RDONLY)
    return -1;
  return (int)fd;
}

static void mark_descriptor(int fd, int inherit) {
  const int flags = fcntl(fd, F_GETFD);
  if (flags >= 0) fcntl(fd, F_SETFD, inherit ? flags & ~FD_CLOEXEC : flags | FD_CLOEXEC);
}

static void prepare_descriptors(int inherit) {
  for (int fd = 0; fd <= 2; ++fd) {
    if (fcntl(fd, F_GETFD) >= 0 || errno != EBADF) continue;
    const int null = open("/dev/null", O_RDWR);
    if (null >= 0 && null != fd) {
      dup2(null, fd);
      close(null);
    }
  }
  DIR* directory = opendir("/proc/self/fd");
  if (!directory) {
    for (int fd = 3; fd < 1024; ++fd) mark_descriptor(fd, fd == inherit);
    return;
  }
  const int own = dirfd(directory);
  struct dirent* entry;
  while ((entry = readdir(directory))) {
    char* end;
    const long fd = strtol(entry->d_name, &end, 10);
    if (end == entry->d_name || *end || fd <= 2 || fd == own || fd > INT_MAX) continue;
    mark_descriptor((int)fd, fd == inherit);
  }
  closedir(directory);
}

static void set_disposition(int signal_number, void (*handler)(int)) {
  struct sigaction action;
  memset(&action, 0, sizeof action);
  action.sa_handler = handler;
  sigemptyset(&action.sa_mask);
  sigaction(signal_number, &action, NULL);
}

static void default_signals(sigset_t* ignored, sigset_t* blocked) {
  sigemptyset(ignored);
  for (int signal_number = 1; signal_number < NSIG; ++signal_number) {
    struct sigaction current;
    if (sigaction(signal_number, NULL, &current) || current.sa_handler != SIG_IGN) continue;
    sigaddset(ignored, signal_number);
    set_disposition(signal_number, SIG_DFL);
  }
  sigset_t none;
  sigemptyset(&none);
  sigprocmask(SIG_SETMASK, &none, blocked);
}

static void reinstate_signals(const sigset_t* ignored, const sigset_t* blocked) {
  for (int signal_number = 1; signal_number < NSIG; ++signal_number)
    if (sigismember(ignored, signal_number) == 1) set_disposition(signal_number, SIG_IGN);
  sigprocmask(SIG_SETMASK, blocked, NULL);
}

static void let_vendor_startup_settle(void) {
  const double minimum = AWTRIX_LOADER_SETTLE_MS / 1000.0;
  const double age = process_age_seconds();
  if (age < 0 || age >= minimum) return;
  note("waiting %.0f ms for zkgui's watchdog-stop thread", (minimum - age) * 1000.0);
  usleep((useconds_t)((minimum - age) * 1e6));
}

static int read_attempts(const struct counter* counter) {
  return loader_combine_attempts(loader_read_attempts(counter->durable),
                                 loader_read_attempts(counter->this_boot));
}

static void clear_attempts(const struct counter* counter) {
  loader_clear_attempts(counter->durable);
  loader_clear_attempts(counter->this_boot);
}

static int record_attempt(const struct counter* counter, int attempt) {
  const int ensured = loader_ensure_directory(LOADER_STATE_DIR, 0755);
  const int stored = ensured ? ensured : loader_store_attempts(counter->durable, attempt);
  if (!stored) {
    loader_clear_attempts(counter->this_boot);
    return 0;
  }
  note("cannot record the start attempt in %s: %s", counter->durable, strerror(-stored));
  const int kept = loader_store_attempts(counter->this_boot, attempt);
  if (kept) {
    note("cannot count the start attempt in %s either: %s", counter->this_boot, strerror(-kept));
    return kept;
  }
  note("counting starts until the next boot in %s", counter->this_boot);
  return 0;
}

static void sleep_ms(unsigned ms) {
  struct timespec left = {ms / 1000, (long)(ms % 1000) * 1000000};
  while (nanosleep(&left, &left) && errno == EINTR) {
  }
}

static pid_t start_rescue_ticks(int fd) {
  const pid_t child = fork();
  if (child) return child;
  prctl(PR_SET_PDEATHSIG, SIGKILL);
  prctl(PR_SET_NAME, "awtrix-rescue-t", 0, 0, 0);
  for (int other = 0; other < 1024; ++other)
    if (other != fd) close(other);
  for (;;) {
    ssize_t written;
    do written = write(fd, "\n", 1);
    while (written < 0 && errno == EINTR);
    if (written < 0) _exit(0);
    sleep_ms(AWTRIX_LOADER_USB_CHECK_MS);
  }
}

static void __attribute__((noreturn)) idle_offline(void) {
  int channel[2];
  if (!pipe(channel) && dup2(channel[0], STDIN_FILENO) == STDIN_FILENO) {
    const pid_t ticks = start_rescue_ticks(channel[1]);
    if (ticks < 0) note("cannot fork the USB check timer: %s", strerror(errno));
    prepare_descriptors(channel[1]);
    sigset_t ignored, blocked;
    default_signals(&ignored, &blocked);
    execl(AWTRIX_LOADER_IDLE_SHELL, "awtrix-loader-rescue", "-c", RESCUE_IDLE_SCRIPT,
          "awtrix-loader-rescue", AWTRIX_LOADER_USB_ROLE, LOADER_RESCUE_MARKER, (char*)NULL);
    const int error = errno;
    reinstate_signals(&ignored, &blocked);
    if (ticks > 0) {
      kill(ticks, SIGKILL);
      waitpid(ticks, NULL, 0);
    }
    note("cannot exec %s: %s; idling inside zkgui", AWTRIX_LOADER_IDLE_SHELL, strerror(error));
  } else {
    note("cannot exec %s without a pipe: %s; idling inside zkgui", AWTRIX_LOADER_IDLE_SHELL,
         strerror(errno));
  }
  flush_journal();
  for (;;) {
    keep_usb_device(1);
    sleep_ms(AWTRIX_LOADER_USB_CHECK_MS);
  }
}

static void __attribute__((noreturn)) enter_rescue(const char* reason) {
  announce_running();
  write_marker(LOADER_RESCUE_MARKER, reason);
  let_vendor_startup_settle();
  char why[160];
  if (rescue_panel_show(AWTRIX_LOADER_PANEL_SPI, AWTRIX_LOADER_GPIO_DIR, why, sizeof why))
    note("cannot show the rescue message: %s", why);
  else
    note("the panel shows USB RECOVERY");
  note("rescue: zkgui idles without the vendor app or its network; repair over USB ADB");
  flush_journal();
  idle_offline();
}

#ifdef AWTRIX_LOADER_TEST_IMAGE_DIR
/* Host tests cannot mount: the verified slot stands for the tree in AWTRIX_LOADER_TEST_IMAGE_DIR,
 * linked where the image would be mounted. */
static int mount_release(const struct release* release, char* why, size_t size) {
  (void)release;
  unlink(LOADER_RELEASE_MOUNT);
  if (symlink(AWTRIX_LOADER_TEST_IMAGE_DIR, LOADER_RELEASE_MOUNT)) {
    snprintf(why, size, "cannot link %s: %s", LOADER_RELEASE_MOUNT, strerror(errno));
    return -1;
  }
  return 0;
}
#else

static int load_loop_module(char* why, size_t size) {
  const int fd = open(AWTRIX_LOADER_LOOP_MODULE, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    snprintf(why, size, "cannot open %s: %s", AWTRIX_LOADER_LOOP_MODULE, strerror(errno));
    return -1;
  }
  const long loaded = syscall(SYS_finit_module, fd, "", 0);
  const int error = errno;
  close(fd);
  if (loaded && error != EEXIST) {
    snprintf(why, size, "cannot load %s: %s", AWTRIX_LOADER_LOOP_MODULE, strerror(error));
    return -1;
  }
  return 0;
}

#ifndef LOOP_SET_DIRECT_IO
#define LOOP_SET_DIRECT_IO 0x4C08
#endif

static void drop_page_cache(void) {
  const int fd = open("/proc/sys/vm/drop_caches", O_WRONLY | O_CLOEXEC);
  if (fd < 0 || write(fd, "1", 1) != 1) note("cannot drop the page cache: %s", strerror(errno));
  if (fd >= 0) close(fd);
}

static int open_node(const char* path, mode_t type, dev_t device, int flags) {
  unlink(path);
  if (mknod(path, type | 0600, device)) return -1;
  return open(path, flags | O_CLOEXEC);
}

/* Read-only loop device over the slot image, detached on unmount; requests direct I/O. */
static int attach_loop(const struct release* release, char* node, size_t node_size, char* why,
                       size_t size) {
  static const char control_path[] = AWTRIX_LOADER_RUNTIME_DIR "/awtrix-loop-control";
  char backing_path[64];
  int control = open("/dev/loop-control", O_RDWR | O_CLOEXEC);
  if (control < 0) control = open_node(control_path, S_IFCHR, makedev(10, 237), O_RDWR);
  unlink(control_path);
  if (control < 0) {
    snprintf(why, size, "no loop control device: %s", strerror(errno));
    return -1;
  }
  const int number = ioctl(control, LOOP_CTL_GET_FREE);
  const int error = errno;
  close(control);
  if (number < 0) {
    snprintf(why, size, "no free loop device: %s", strerror(error));
    return -1;
  }
  snprintf(node, node_size, "%s/awtrix-loop%d", AWTRIX_LOADER_RUNTIME_DIR, number);
  const int loop = open_node(node, S_IFBLK, makedev(7, (unsigned)number), O_RDONLY);
  if (loop < 0) {
    snprintf(why, size, "cannot open loop device %d: %s", number, strerror(errno));
    return -1;
  }
  snprintf(backing_path, sizeof backing_path, "%s/mtdblock%u", AWTRIX_LOADER_MTD_BLOCK_DIR, release->index);
  const int backing = open(backing_path, O_RDONLY | O_CLOEXEC);
  if (backing < 0) {
    snprintf(why, size, "cannot open mtdblock%u: %s", release->index, strerror(errno));
    close(loop);
    return -1;
  }
  struct loop_info64 info;
  memset(&info, 0, sizeof info);
  info.lo_offset = release->layout.image;
  info.lo_sizelimit = release->header.image_bytes;
  info.lo_flags = LO_FLAGS_READ_ONLY | LO_FLAGS_AUTOCLEAR;
  const int attached = ioctl(loop, LOOP_SET_FD, backing);
  const int set = attached ? -1 : ioctl(loop, LOOP_SET_STATUS64, &info);
  const int failure = errno;
  close(backing);
  if (attached || set) {
    if (!attached) ioctl(loop, LOOP_CLR_FD, 0);
    close(loop);
    snprintf(why, size, "cannot attach the release image: %s", strerror(failure));
    return -1;
  }
  if (ioctl(loop, LOOP_SET_DIRECT_IO, 1UL)) {
    note("no direct I/O for the release image (%s): dropping the page cache", strerror(errno));
    drop_page_cache();
  }
  return loop;
}

static int mount_release(const struct release* release, char* why, size_t size) {
  if (load_loop_module(why, size)) return -1;
  mkdir(LOADER_RELEASE_MOUNT, 0755);
  if (tc002_mounted(LOADER_RELEASE_MOUNT) && umount2(LOADER_RELEASE_MOUNT, MNT_DETACH))
    note("cannot detach the release mounted before: %s", strerror(errno));
  char node[sizeof AWTRIX_LOADER_RUNTIME_DIR + 32];
  const int loop = attach_loop(release, node, sizeof node, why, size);
  if (loop < 0) return -1;
  const int result = mount(node, LOADER_RELEASE_MOUNT, "squashfs", MS_RDONLY | MS_NODEV | MS_NOSUID, NULL);
  const int error = errno;
  if (result) ioctl(loop, LOOP_CLR_FD, 0);
  close(loop);
  unlink(node);
  if (result) {
    snprintf(why, size, "cannot mount the release image: %s", strerror(error));
    return -1;
  }
  return 0;
}
#endif

static void __attribute__((noreturn)) start_daemon(const struct release* release, int attempts) {
  announce_running();
  const int attempt = attempts + 1;
  if (record_attempt(&boot_counter, attempt)) enter_rescue("cannot count the start attempt: rescue");
  char why[256], reason[320];
  if (mount_release(release, why, sizeof why)) {
    snprintf(reason, sizeof reason, "%s: the AWTRIX release cannot be mounted: rescue", why);
    note("%s", reason);
    enter_rescue(reason);
  }
  static char daemon[] = LOADER_RELEASE_DAEMON;
  if (!regular_file(daemon) || access(daemon, X_OK)) {
    const char* problem = LOADER_RELEASE_DAEMON " is missing or not executable: the AWTRIX release is not runnable: rescue";
    note("%s", problem);
    enter_rescue(problem);
  }
  let_vendor_startup_settle();
  note("exec %s of release %s (start %d of %d)", daemon, release->header.release, attempt,
       LOADER_ATTEMPT_LIMIT);
  flush_journal();
  prepare_descriptors(property_workspace_descriptor());
  sigset_t ignored, blocked;
  default_signals(&ignored, &blocked);
  static char root[] = LOADER_RELEASE_MOUNT, data[] = AWTRIX_LOADER_DATA_DIR, root_flag[] = "--root",
              data_flag[] = "--data", boot_flag[] = "--boot";
  char* const arguments[] = {daemon, root_flag, root, data_flag, data, boot_flag, NULL};
  execv(daemon, arguments);
  const int error = errno;
  reinstate_signals(&ignored, &blocked);
  snprintf(reason, sizeof reason, "exec failed (%s): rescue", strerror(error));
  note("%s", reason);
  enter_rescue(reason);
}

static void hold_for_deploy(struct deploy_token_watch* watch) {
  announce_running();
  flush_journal();
  const double end = tc002_clock_seconds(CLOCK_MONOTONIC) + AWTRIX_LOADER_DEPLOY_HOLD_LIMIT_SECONDS;
  double usb_at = 0;
  int active;
  while ((active = deploy_token_file_active(LOADER_DEPLOY_TOKEN, watch,
                                            AWTRIX_LOADER_DEPLOY_STALE_SECONDS, 1)) &&
         tc002_clock_seconds(CLOCK_MONOTONIC) < end) {
    const double now = tc002_clock_seconds(CLOCK_MONOTONIC);
    if (now >= usb_at) {
      keep_usb_device(0);
      usb_at = now + AWTRIX_LOADER_USB_CHECK_MS / 1000.0;
    }
    sleep_ms(AWTRIX_LOADER_DEPLOY_CHECK_MS);
  }
  note("%s: deciding again", active ? "USB deploy hold reached its time limit"
                             : present(LOADER_DEPLOY_TOKEN) ? "deploy token went stale"
                                                            : "deploy token removed");
}

static void start_vendor(void* context) {
  vendor.library = dlopen(AWTRIX_LOADER_VENDOR_LIBRARY, RTLD_LAZY);
  if (!vendor.library) {
    const char* reason = "vendor app cannot be loaded: rescue";
    note("cannot load %s: %s; %s", AWTRIX_LOADER_VENDOR_LIBRARY, dlerror(), reason);
    enter_rescue(reason);
  }
  vendor.init = (easyui_hook)dlsym(vendor.library, "onEasyUIInit");
  vendor.deinit = (easyui_hook)dlsym(vendor.library, "onEasyUIDeinit");
  vendor.startup = (easyui_startup)dlsym(vendor.library, "onStartupApp");
  announce_running();
  note("vendor app loaded from %s", AWTRIX_LOADER_VENDOR_LIBRARY);
  flush_journal();
  if (vendor.init) vendor.init(context);
}

static void inspect(struct loader_facts* facts, struct release* release, char* update,
                    size_t update_size) {
  facts->upgrade_pending = upgrade_pending(update, update_size);
  facts->vendor_this_boot = !access(LOADER_VENDOR_MARKER, F_OK);
  facts->release = LOADER_RELEASE_MISSING;
  facts->boot_attempts = 0;
  if (facts->upgrade_pending || facts->knob_held || facts->vendor_this_boot || facts->deploy_active)
    return;
  inspect_release(release);
  facts->release = release->state;
  facts->boot_attempts = read_attempts(&boot_counter);
}

static enum loader_choice decide(int knob, struct loader_facts* facts, const struct release* release,
                                 const char* update, char* why, size_t why_size) {
  const enum loader_choice choice = loader_decide(facts);
  if (loader_choice_rescues(choice) && release->problem[0])
    snprintf(why, why_size, "%s; %s", release->problem, loader_choice_reason(choice));
  else
    snprintf(why, why_size, "%s", loader_choice_reason(choice));
  note("knob=%s update=%s release=%s%s%s attempts=%d: %s", knob < 0 ? "no-device" : knob ? "held" : "released",
       facts->upgrade_pending ? update : "none", loader_release_name(facts->release),
       facts->release == LOADER_RELEASE_VALID ? " " : "",
       facts->release == LOADER_RELEASE_VALID ? release->header.release : "", facts->boot_attempts, why);
  return choice;
}

LOADER_EXPORT void onEasyUIInit(void* context) {
  select_usb_device();
  static struct release release;
  char update[PATH_MAX] = "", why[320];
  struct loader_facts facts;
  memset(&facts, 0, sizeof facts);
  struct deploy_token_watch deploy;
  memset(&deploy, 0, sizeof deploy);
  const int knob = knob_state();
  facts.knob_held = knob == 1;
  facts.deploy_active = deploy_token_file_active(LOADER_DEPLOY_TOKEN, &deploy,
                                                  AWTRIX_LOADER_DEPLOY_STALE_SECONDS, 1);
  inspect(&facts, &release, update, sizeof update);
  enum loader_choice choice = decide(knob, &facts, &release, update, why, sizeof why);
  if (choice == LOADER_HOLD_DEPLOY) {
    hold_for_deploy(&deploy);
    facts.deploy_active = 0;
    inspect(&facts, &release, update, sizeof update);
    choice = decide(knob, &facts, &release, update, why, sizeof why);
  }
  if (loader_choice_resets_attempts(choice)) clear_attempts(&boot_counter);
  if (loader_choice_rescues(choice)) enter_rescue(why);
  unlink(LOADER_RESCUE_MARKER);
  if (choice == LOADER_START_DAEMON) start_daemon(&release, facts.boot_attempts);
  if (loader_choice_sticks_this_boot(choice))
    write_marker(LOADER_VENDOR_MARKER, loader_choice_reason(choice));
  start_vendor(context);
}

LOADER_EXPORT void onEasyUIDeinit(void* context) {
  if (vendor.deinit) vendor.deinit(context);
}

LOADER_EXPORT const char* onStartupApp(void* context) {
  if (vendor.startup) return vendor.startup(context);
  return "mainActivity";
}
