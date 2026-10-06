/* Host stand-in for awtrix-tc002-audio-pcm: the real helper core and loop over the fake
 * awtrix_pcm device, which plays in real time. Fails when sound was written while muted or the
 * blocking DRAIN ioctl was used. */
#define _GNU_SOURCE
#include <stdio.h>

#include "platform/tc002/audio/helper/audio_process.h"
#include "platform/tc002/audio/pcm/pcm_backend.h"
#include "fake_pcm_device.h"

static fake_pcm device;
static ap_helper helper;
static int channel = -1;

static int64_t device_clock(fake_pcm *f) {
  (void)f;
  return ah_monotonic_ms();
}

static int64_t now_ms(void *context) {
  (void)context;
  return ah_monotonic_ms();
}

static int send_message(void *context, const uint8_t *data, size_t size) {
  (void)context;
  return ah_send_datagram(channel, data, size);
}

int main(int argc, char **argv) {
  static const ap_ops ops = {&device, now_ms, send_message, fake_pcm_ioctl, fake_pcm_write};
  sigset_t wait_mask;
  int code;
  if (!ah_install_signals(&wait_mask)) return AH_EXIT_USAGE;
  channel = ah_parse_socket(argc, argv);
  if (channel < 0) return AH_EXIT_USAGE;
  fake_pcm_reset(&device, device_clock, NULL, 0);
  if (!ap_start(&helper, &ops)) {
    ah_flush(&helper.core);
    return AH_EXIT_START;
  }
  code = ah_run(&helper.core, channel, -1, NULL, &wait_mask);
  if (ap_shutdown(&helper) && code == AH_EXIT_CLEAN) code = AH_EXIT_FAILED;
  fprintf(stderr, "fake pcm helper: %lu audible writes, gain %d dB, %u stops, %u underruns\n",
          device.nonsilent_writes, (int)device.gain_db, device.stops, device.underruns);
  if (device.muted_sound || device.drains || device.state != AWTRIX_PCM_PREPARED) return AH_EXIT_FAILED;
  return code;
}
