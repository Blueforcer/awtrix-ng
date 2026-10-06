/* awtrix-tc002-audio-pcm: the speaker helper for the awtrix_pcm kernel driver, static musl.
 * It owns /dev/awtrix_pcm for the lifetime of one inherited SOCK_SEQPACKET socket. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../helper/audio_owner.h"
#include "../helper/audio_process.h"
#include "pcm_backend.h"

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error The helper protocol and awtrix_pcm take little-endian PCM
#endif

static int channel = -1, device = -1;
static ap_helper helper;

static int64_t native_now(void *context) {
  (void)context;
  return ah_monotonic_ms();
}

static int native_send(void *context, const uint8_t *data, size_t size) {
  (void)context;
  return ah_send_datagram(channel, data, size);
}

static int native_ioctl(void *context, unsigned request, void *argument) {
  (void)context;
  return ioctl(device, request, argument);
}

static ssize_t native_write(void *context, const void *data, size_t size) {
  (void)context;
  return write(device, data, size);
}

/* Nobody else may hold the output: no stock GUI, and no audio DMA interrupt, which exists only
 * while an output is open. */
static int output_free(void) {
  return ah_stock_gui_absent("/proc") && ah_file_contains("/proc/interrupts", "aio_dma") == 0;
}

static int open_device(void) {
  struct stat st;
  const int fd = open(AWTRIX_PCM_DEVICE, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW);
  if (fd < 0) return -errno;
  if (fstat(fd, &st) || !S_ISCHR(st.st_mode)) {
    close(fd);
    return -ENODEV;
  }
  return fd;
}

int main(int argc, char **argv) {
  static const ap_ops ops = {NULL, native_now, native_send, native_ioctl, native_write};
  extern char **environ;
  sigset_t wait_mask;
  int code;
  if (environ && environ[0]) return AH_EXIT_USAGE;
  if (!ah_install_signals(&wait_mask)) return AH_EXIT_USAGE;
  channel = ah_parse_socket(argc, argv);
  if (channel < 0 || !ah_silence_stdio()) return AH_EXIT_USAGE;
  alarm(AH_START_BUDGET_SECONDS);
  if (getuid() != 0 || geteuid() != 0 || !ah_board_matches() || !output_free()) {
    ah_report_failure(&helper.core, channel, TC002_AUDIO_ERROR_PREFLIGHT, 0);
    return AH_EXIT_PREFLIGHT;
  }
  device = open_device();
  if (device < 0) {
    ah_report_failure(&helper.core, channel, TC002_AUDIO_ERROR_VENDOR, device);
    return AH_EXIT_DEVICE;
  }
  if (!ap_start(&helper, &ops)) {
    ah_flush(&helper.core);
    code = AH_EXIT_START;
  } else {
    alarm(0);
    code = ah_run(&helper.core, channel, device, ap_device_failed, &wait_mask);
  }
  alarm(AH_START_BUDGET_SECONDS);
  if (ap_shutdown(&helper) && code == AH_EXIT_CLEAN) code = AH_EXIT_FAILED;
  close(device);
  return code;
}
