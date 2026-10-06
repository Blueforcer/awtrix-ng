#ifndef AWTRIX_TC002_AUDIO_PCM_BACKEND_H
#define AWTRIX_TC002_AUDIO_PCM_BACKEND_H

/* The awtrix_pcm device behind the speaker helper's protocol core: the ah_state machine (queue,
 * resampler, priming, settling, status) drives it, and its device calls map onto the driver's
 * write() and ioctls. Nothing here blocks: writes are non-blocking, the driver's
 * blocking DRAIN is never used, and a stream ends with STOP once only its silent tail is left. */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "../../kmod/awtrix_pcm/awtrix_pcm.h"
#include "../helper/audio_helper.h"

enum {
  AP_GUARD_BYTES = 256,
  AP_STALL_MS = 1000
};

/* ioctl and write return -1 and set errno like the system calls on the device descriptor. */
typedef struct {
  void *context;
  int64_t (*now_ms)(void *);
  int (*send)(void *, const uint8_t *, size_t);
  int (*ioctl)(void *, unsigned request, void *argument);
  ssize_t (*write)(void *, const void *data, size_t size);
} ap_ops;

typedef struct {
  ah_state core;
  ah_ops bridge;
  const ap_ops *ops;
  uint8_t carry[AH_BLOCK_BYTES];
  size_t carry_offset, carry_length;
  uint32_t driver_underruns;
  int stall_armed;
  uint32_t stall_level;
  int64_t stall_since;
} ap_helper;

/* Mutes the freshly opened device at the lowest gain and announces HELLO. False leaves the
 * core FAILED; ap_shutdown is still due. */
int ap_start(ap_helper *h, const ap_ops *ops);
/* Mutes and stops the device so that close() returns at once; counts the calls that failed. */
unsigned ap_shutdown(ap_helper *h);
/* For ah_run: the device reported POLLERR, so the driver has failed. */
void ap_device_failed(ah_state *s);

#endif
