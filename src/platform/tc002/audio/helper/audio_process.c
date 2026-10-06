#define _GNU_SOURCE
#include "audio_process.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../../contract/tc002_layout.h"
#include "audio_owner.h"

static const char board_model[] = TC002_BOARD_MODEL;
static volatile sig_atomic_t stop_signal;
static int report_channel = -1;

static void on_stop(int number) { stop_signal = number; }
static void on_alarm(int number) {
  (void)number;
  _exit(124);
}

int64_t ah_monotonic_ms(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
  return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int ah_install_signals(sigset_t *wait_mask) {
  struct sigaction action;
  sigset_t blocked;
  struct rlimit core = {0, 0};
  memset(&action, 0, sizeof action);
  sigemptyset(&action.sa_mask);
  action.sa_handler = on_alarm;
  if (sigaction(SIGALRM, &action, NULL)) return 0;
  action.sa_handler = on_stop;
  if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL)) return 0;
  action.sa_handler = SIG_IGN;
  if (sigaction(SIGHUP, &action, NULL) || sigaction(SIGPIPE, &action, NULL)) return 0;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGTERM);
  sigaddset(&blocked, SIGINT);
  if (sigprocmask(SIG_BLOCK, &blocked, wait_mask)) return 0;
  sigdelset(wait_mask, SIGTERM);
  sigdelset(wait_mask, SIGINT);
  return !setrlimit(RLIMIT_CORE, &core);
}

int ah_parse_socket(int argc, char **argv) {
  char *end;
  long fd;
  int type = 0, domain = 0, flags;
  socklen_t length = sizeof type;
  struct stat st;
  if (argc != 3 || strcmp(argv[1], "--socket-fd")) return -1;
  errno = 0;
  fd = strtol(argv[2], &end, 10);
  if (errno || *end || !ah_numeric(argv[2]) || fd < 3 || fd > 1023) return -1;
  if (fstat((int)fd, &st) || !S_ISSOCK(st.st_mode)) return -1;
  if (getsockopt((int)fd, SOL_SOCKET, SO_TYPE, &type, &length) || type != SOCK_SEQPACKET) return -1;
  length = sizeof domain;
  if (getsockopt((int)fd, SOL_SOCKET, SO_DOMAIN, &domain, &length) || domain != AF_UNIX) return -1;
  flags = fcntl((int)fd, F_GETFL);
  if (flags < 0 || fcntl((int)fd, F_SETFD, FD_CLOEXEC) || fcntl((int)fd, F_SETFL, flags | O_NONBLOCK))
    return -1;
  return (int)fd;
}

int ah_silence_stdio(void) {
  const int sink = open("/dev/null", O_RDWR | O_CLOEXEC);
  int okay;
  if (sink < 0) return 0;
  okay = dup2(sink, STDIN_FILENO) >= 0 && dup2(sink, STDOUT_FILENO) >= 0 &&
         dup2(sink, STDERR_FILENO) >= 0;
  close(sink);
  return okay;
}

int ah_board_matches(void) {
  uint8_t model[128];
  size_t size = 0;
  if (!ah_read_small("/proc/device-tree/model", model, sizeof model, &size)) return 0;
  return (size == sizeof board_model - 1 || size == sizeof board_model) &&
         !memcmp(model, board_model, sizeof board_model - 1) &&
         (size == sizeof board_model - 1 || model[size - 1] == 0);
}

int ah_send_datagram(int fd, const uint8_t *data, size_t size) {
  for (;;) {
    const ssize_t n = send(fd, data, size, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n == (ssize_t)size) return 1;
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)) return 0;
    return -1;
  }
}

static int64_t report_now(void *context) {
  (void)context;
  return ah_monotonic_ms();
}

static int report_send(void *context, const uint8_t *data, size_t size) {
  (void)context;
  return ah_send_datagram(report_channel, data, size);
}

void ah_report_failure(ah_state *s, int channel, uint8_t error, int32_t vendor_result) {
  static const ah_ops ops = {NULL, report_now, NULL, report_send};
  report_channel = channel;
  ah_init(s, &ops);
  ah_fail(s, error, vendor_result);
  ah_flush(s);
}

/* Returns 0 on socket EOF, 1 to keep going, -1 on a socket error. */
static int receive_all(ah_state *s, int channel, int64_t now) {
  uint8_t message[TC002_AUDIO_MAX_MESSAGE + 1];
  for (;;) {
    const ssize_t n = recv(channel, message, sizeof message, MSG_DONTWAIT | MSG_TRUNC);
    if (n == 0) return 0;
    if (n < 0) {
      if (errno == EINTR) continue;
      return errno == EAGAIN || errno == EWOULDBLOCK ? 1 : -1;
    }
    alarm(AH_CALL_BUDGET_SECONDS);
    ah_receive(s, message, (size_t)n > TC002_AUDIO_MAX_MESSAGE ? 0 : (size_t)n, now);
    alarm(0);
    if (s->state == TC002_AUDIO_FAILED) return 1;
  }
}

int ah_run(ah_state *s, int channel, int device, void (*device_failed)(ah_state *),
           const sigset_t *wait_mask) {
  int wait = -1;
  for (;;) {
    struct pollfd fds[2] = {{channel, POLLIN, 0}, {device, 0, 0}};
    struct timespec timeout, *limit = NULL;
    int ready, received;
    ah_flush(s);
    if (s->broken) return AH_EXIT_FAILED;
    if (s->state == TC002_AUDIO_FAILED) return AH_EXIT_FAILED;
    if (s->hello_pending || s->status_dirty) fds[0].events |= POLLOUT;
    if (wait >= 0) {
      timeout.tv_sec = wait / 1000;
      timeout.tv_nsec = (long)(wait % 1000) * 1000000L;
      limit = &timeout;
    }
    ready = ppoll(fds, device >= 0 ? 2 : 1, limit, wait_mask);
    if (stop_signal) return AH_EXIT_CLEAN;
    if (ready < 0 && errno != EINTR) return AH_EXIT_FAILED;
    if (ready > 0 && (device < 0 || fds[0].revents)) {
      received = receive_all(s, channel, ah_monotonic_ms());
      if (received < 0) return AH_EXIT_FAILED;
      if (!received) return AH_EXIT_CLEAN;
      if (fds[0].revents & (POLLERR | POLLNVAL)) return AH_EXIT_FAILED;
    }
    alarm(AH_CALL_BUDGET_SECONDS);
    if (ready > 0 && device >= 0 && (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))) {
      if (device_failed) device_failed(s);
      if (s->state != TC002_AUDIO_FAILED) ah_fail(s, TC002_AUDIO_ERROR_VENDOR, -EIO);
    }
    wait = s->state == TC002_AUDIO_FAILED ? -1 : ah_service(s, ah_monotonic_ms());
    alarm(0);
  }
}
