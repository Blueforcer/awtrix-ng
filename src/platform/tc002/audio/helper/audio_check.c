/* awtrix-tc002-audio-check: runs the speaker helper the way the supervisor will (empty
 * environment, one inherited SOCK_SEQPACKET descriptor) and plays one finite sine tone.
 * Usage: awtrix-tc002-audio-check HELPER VOLUME HZ MS [STOP_AFTER_MS] */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../../contract/AudioProtocol.h"

enum { HELPER_FD = 104, AMPLITUDE = 8000, FADE_FRAMES = 441, SINE_STEPS = 1024 };

static int channel = -1;
static tc002_audio_status last;
static uint32_t last_generation;
static int statuses;

static int64_t now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static const char *state_name(uint8_t state) {
  static const char *const names[] = {"starting", "idle", "priming", "playing", "settling", "paused", "failed"};
  return state <= TC002_AUDIO_FAILED ? names[state] : "?";
}

static int number(const char *text, long low, long high, long *out) {
  char *end;
  errno = 0;
  *out = strtol(text, &end, 10);
  return !errno && *text && !*end && *out >= low && *out <= high;
}

/* One sine period from a second-order resonator, so no libm is needed. */
static int16_t sine(uint32_t phase) {
  static int16_t table[SINE_STEPS];
  static int ready;
  if (!ready) {
    double s0 = 0, s1 = -0.0061358846491544753, k = 2 * 0.99998117528260111;
    for (int i = 0; i < SINE_STEPS; ++i) {
      const double s = k * s0 - s1;
      table[i] = (int16_t)(s0 * 32767.0);
      s1 = s0;
      s0 = s;
    }
    ready = 1;
  }
  return table[phase >> 22];
}

static int send_message(const uint8_t *data, size_t size) {
  for (;;) {
    const ssize_t n = send(channel, data, size, MSG_NOSIGNAL);
    if (n == (ssize_t)size) return 1;
    if (n < 0 && errno == EINTR) continue;
    return 0;
  }
}

/* Reads everything pending; returns 0 when the helper is gone. */
static int receive(int wait_ms, int *hello) {
  struct pollfd p = {channel, POLLIN, 0};
  uint8_t message[TC002_AUDIO_MAX_MESSAGE + 1];
  if (poll(&p, 1, wait_ms) < 0 && errno != EINTR) return 0;
  for (;;) {
    tc002_audio_frame frame;
    const ssize_t n = recv(channel, message, sizeof message, MSG_DONTWAIT | MSG_TRUNC);
    if (n == 0) return 0;
    if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    if ((size_t)n > TC002_AUDIO_MAX_MESSAGE || !tc002_audio_parse(message, (size_t)n, &frame)) continue;
    if (frame.type == TC002_AUDIO_HELLO) {
      tc002_audio_hello h;
      if (tc002_audio_read_hello(&frame, &h)) {
        printf("hello rate=%u channels=%u gain=%d..%d dB window=%u\n", h.device_rate, h.device_channels,
               h.min_db, h.max_db, h.window_bytes);
        *hello = 1;
      }
    } else if (frame.type == TC002_AUDIO_STATUS) {
      tc002_audio_status s;
      if (!tc002_audio_read_status(&frame, &s)) continue;
      if (!statuses || s.state != last.state || s.error != last.error)
        printf("status state=%s error=%u vendor=0x%08x busy=%u completed=%u underruns=%u\n",
               state_name(s.state), s.error, (unsigned)s.vendor_result, s.device_busy_bytes,
               s.completed_generation, s.underruns);
      last = s;
      last_generation = frame.generation;
      ++statuses;
    }
  }
}

static int reap(pid_t helper) {
  int status = 0;
  for (int waited = 0; waited < 5000; waited += 10) {
    const pid_t r = waitpid(helper, &status, WNOHANG);
    if (r == helper) {
      if (WIFEXITED(status)) printf("helper exit=%d\n", WEXITSTATUS(status));
      else printf("helper signal=%d\n", WTERMSIG(status));
      return WIFEXITED(status) && !WEXITSTATUS(status);
    }
    usleep(10000);
  }
  printf("helper still running after 5 s: check /proc/%d/stat for state D before anything else\n", (int)helper);
  return 0;
}

int main(int argc, char **argv) {
  long volume, hz, ms, stop_after = -1;
  int pair[2], hello = 0;
  pid_t helper;
  uint8_t m[TC002_AUDIO_MAX_MESSAGE];
  int16_t block[TC002_AUDIO_MAX_PCM_BYTES / 2];
  uint32_t sent = 0, frames, produced = 0, phase = 0, step;
  int64_t started, deadline;
  if ((argc != 5 && argc != 6) || !number(argv[2], 0, 100, &volume) || !number(argv[3], 50, 8000, &hz) ||
      !number(argv[4], 50, 10000, &ms) || (argc == 6 && !number(argv[5], 0, 10000, &stop_after))) {
    fputs("usage: awtrix-tc002-audio-check HELPER VOLUME(0-100) HZ(50-8000) MS(50-10000) [STOP_AFTER_MS]\n", stderr);
    return 2;
  }
  setvbuf(stdout, NULL, _IOLBF, 0);
  signal(SIGPIPE, SIG_IGN);
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair)) return 1;
  helper = fork();
  if (helper < 0) return 1;
  if (!helper) {
    char *const args[] = {argv[1], (char *)"--socket-fd", (char *)"104", NULL};
    char *const empty[] = {NULL};
    if (dup2(pair[1], HELPER_FD) != HELPER_FD) _exit(127);
    execve(argv[1], args, empty);
    _exit(127);
  }
  close(pair[1]);
  channel = pair[0];

  deadline = now_ms() + 5000;
  while (!hello && last.state != TC002_AUDIO_FAILED && now_ms() < deadline)
    if (!receive(50, &hello)) break;
  if (!hello) {
    printf("no hello from the helper\n");
    close(channel);
    reap(helper);
    return 1;
  }

  send_message(m, tc002_audio_encode_volume(m, sizeof m, (uint8_t)volume));
  send_message(m, tc002_audio_encode_open(m, sizeof m, 1, TC002_AUDIO_DEVICE_RATE, 1));
  frames = (uint32_t)(ms * TC002_AUDIO_DEVICE_RATE / 1000);
  step = (uint32_t)(((uint64_t)hz << 32) / TC002_AUDIO_DEVICE_RATE);
  started = now_ms();
  while (produced < frames && last.state != TC002_AUDIO_FAILED) {
    const uint32_t consumed = last_generation == 1 ? last.consumed_bytes : 0;
    const uint32_t room = TC002_AUDIO_WINDOW_BYTES - (sent - consumed);
    uint32_t count = frames - produced, i;
    if (stop_after >= 0 && now_ms() - started >= stop_after) break;
    if (count > room / 2) count = room / 2;
    if (count > TC002_AUDIO_MAX_PCM_BYTES / 2) count = TC002_AUDIO_MAX_PCM_BYTES / 2;
    if (!count) {
      if (!receive(20, &hello)) break;
      continue;
    }
    for (i = 0; i < count; ++i, ++produced, phase += step) {
      const uint32_t edge = produced < frames - 1 - produced ? produced : frames - 1 - produced;
      const int32_t envelope = edge < FADE_FRAMES ? (int32_t)edge : FADE_FRAMES;
      block[i] = (int16_t)((int32_t)sine(phase) * AMPLITUDE / 32767 * envelope / FADE_FRAMES);
    }
    if (!send_message(m, tc002_audio_encode_pcm(m, sizeof m, 1, block, count))) break;
    sent += count * 2;
    if (!receive(0, &hello)) break;
  }
  if (stop_after >= 0) {
    while (now_ms() - started < stop_after && receive(10, &hello)) {
    }
    printf("stop after %ld ms\n", (long)(now_ms() - started));
    send_message(m, tc002_audio_encode_empty(m, sizeof m, TC002_AUDIO_STOP, 1));
  } else {
    send_message(m, tc002_audio_encode_empty(m, sizeof m, TC002_AUDIO_DRAIN, 1));
  }
  deadline = now_ms() + ms + 3000;
  while (last.completed_generation != 1 && last.state != TC002_AUDIO_FAILED && now_ms() < deadline)
    if (!receive(20, &hello)) break;
  printf("%s after %ld ms, underruns=%u\n", last.completed_generation == 1 ? "completed" : "NOT completed",
         (long)(now_ms() - started), last.underruns);
  close(channel);
  return reap(helper) && last.completed_generation == 1 ? 0 : 1;
}
