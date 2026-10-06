#include "pcm_backend.h"

#include <errno.h>
#include <string.h>

_Static_assert(sizeof(struct awtrix_pcm_status) == 48, "awtrix_pcm status layout");

static int64_t bridge_now(void *context) {
  const ap_helper *h = context;
  return h->ops->now_ms(h->ops->context);
}

static int bridge_send(void *context, const uint8_t *data, size_t size) {
  const ap_helper *h = context;
  return h->ops->send(h->ops->context, data, size);
}

static int device_ioctl(ap_helper *h, unsigned request, void *argument) {
  for (;;) {
    if (!h->ops->ioctl(h->ops->context, request, argument)) return 0;
    if (errno != EINTR) return -1;
  }
}

static ssize_t device_write(ap_helper *h, const void *data, size_t size) {
  for (;;) {
    const ssize_t n = h->ops->write(h->ops->context, data, size);
    if (n >= 0 || errno != EINTR) return n;
  }
}

static int read_status(ap_helper *h, struct awtrix_pcm_status *status) {
  memset(status, 0, sizeof *status);
  return device_ioctl(h, AWTRIX_PCM_GET_STATUS, status);
}

/* The vendor result of a failed call: the driver's HAL error when it has one, else -errno. */
static int32_t failure(ap_helper *h) {
  const int error = errno;
  struct awtrix_pcm_status status;
  if (error == EIO && !read_status(h, &status) && status.hal_error) return status.hal_error;
  return error ? -error : -EIO;
}

static int32_t control(ap_helper *h, unsigned request, void *argument) {
  return device_ioctl(h, request, argument) ? failure(h) : 0;
}

static int32_t flush_carry(ap_helper *h) {
  ssize_t n;
  if (h->carry_offset == h->carry_length) return 0;
  n = device_write(h, h->carry + h->carry_offset, h->carry_length - h->carry_offset);
  if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : failure(h);
  h->carry_offset += (size_t)n;
  if (h->carry_offset == h->carry_length) h->carry_offset = h->carry_length = 0;
  return 0;
}

static void note_underruns(ap_helper *h, uint32_t underruns) {
  if (underruns == h->driver_underruns) return;
  h->core.underruns += underruns - h->driver_underruns;
  h->driver_underruns = underruns;
  h->core.status_dirty = 1;
}

static int stalled(ap_helper *h, const struct awtrix_pcm_status *status) {
  const int64_t now = bridge_now(h);
  if (status->state != AWTRIX_PCM_RUNNING || !status->queued_bytes) {
    h->stall_armed = 0;
    return 0;
  }
  if (!h->stall_armed || status->queued_bytes != h->stall_level) {
    h->stall_armed = 1;
    h->stall_level = status->queued_bytes;
    h->stall_since = now;
    return 0;
  }
  return now - h->stall_since >= AP_STALL_MS;
}

static int32_t query(ap_helper *h, void *out) {
  struct awtrix_pcm_status status;
  uint32_t words[3], busy;
  const int32_t carried = flush_carry(h);
  if (carried) return carried;
  if (read_status(h, &status)) return failure(h);
  if (status.state == AWTRIX_PCM_FAILED) return status.hal_error ? status.hal_error : -EIO;
  if (!status.buffer_bytes || status.buffer_bytes % 16u) return -EPROTO;
  note_underruns(h, status.underruns);
  if (stalled(h, &status)) return -ETIMEDOUT;
  busy = (status.queued_bytes + (uint32_t)(h->carry_length - h->carry_offset)) & ~15u;
  if (busy > status.buffer_bytes) busy = status.buffer_bytes;
  words[0] = status.buffer_bytes;
  words[1] = status.buffer_bytes - busy;
  words[2] = busy;
  memcpy(out, words, sizeof words);
  return 0;
}

/* The core expects a block to be taken whole or refused; a partial write keeps the rest in the
 * carry, which goes out before anything else. */
static int32_t send_block(ap_helper *h, const void *block, size_t size) {
  ssize_t n;
  int32_t result;
  if (size > sizeof h->carry) return -EINVAL;
  result = flush_carry(h);
  if (result) return result;
  if (h->carry_length) return AH_DEVICE_FULL;
  n = device_write(h, block, size);
  if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? AH_DEVICE_FULL : failure(h);
  if ((size_t)n < size) {
    memcpy(h->carry, (const uint8_t *)block + n, size - (size_t)n);
    h->carry_offset = 0;
    h->carry_length = size - (size_t)n;
  }
  return 0;
}

static int32_t bridge_call(void *context, enum ah_call which, void *data, int32_t value) {
  ap_helper *h = context;
  switch (which) {
    case AH_SET_MUTE: {
      uint32_t muted = value ? 1u : 0u;
      return control(h, AWTRIX_PCM_SET_MUTE, &muted);
    }
    case AH_SET_VOLUME: {
      int32_t db = value;
      return control(h, AWTRIX_PCM_SET_GAIN, &db);
    }
    case AH_CLEAR:
      h->carry_offset = h->carry_length = 0;
      h->stall_armed = 0;
      return control(h, AWTRIX_PCM_STOP, NULL);
    case AH_QUERY: return query(h, data);
    case AH_SEND: return value < 0 ? -EINVAL : send_block(h, data, (size_t)value);
    default: return -EINVAL;
  }
}

int ap_start(ap_helper *h, const ap_ops *ops) {
  struct awtrix_pcm_status status;
  uint32_t muted = 1;
  int32_t db = ah_volume_db(0), result;
  memset(h, 0, sizeof *h);
  h->ops = ops;
  h->bridge.context = h;
  h->bridge.now_ms = bridge_now;
  h->bridge.call = bridge_call;
  h->bridge.send = bridge_send;
  ah_init(&h->core, &h->bridge);
  if (read_status(h, &status)) {
    ah_fail(&h->core, TC002_AUDIO_ERROR_VENDOR, failure(h));
    return 0;
  }
  if (status.state != AWTRIX_PCM_PREPARED || status.queued_bytes || status.buffer_bytes % 16u ||
      status.buffer_bytes < AH_TARGET_BUSY_BYTES + AP_GUARD_BYTES) {
    ah_fail(&h->core, TC002_AUDIO_ERROR_QUEUE, status.hal_error);
    return 0;
  }
  h->driver_underruns = status.underruns;
  if ((result = control(h, AWTRIX_PCM_SET_MUTE, &muted)) || (result = control(h, AWTRIX_PCM_SET_GAIN, &db))) {
    ah_fail(&h->core, TC002_AUDIO_ERROR_VENDOR, result);
    return 0;
  }
  if (read_status(h, &status)) {
    ah_fail(&h->core, TC002_AUDIO_ERROR_VENDOR, failure(h));
    return 0;
  }
  if (!status.muted || status.gain_db != db) {
    ah_fail(&h->core, TC002_AUDIO_ERROR_VENDOR, status.hal_error);
    return 0;
  }
  h->core.state = TC002_AUDIO_IDLE;
  h->core.hello_pending = 1;
  return 1;
}

unsigned ap_shutdown(ap_helper *h) {
  uint32_t muted = 1;
  unsigned failures = 0;
  if (!h->ops) return 0;
  if (device_ioctl(h, AWTRIX_PCM_SET_MUTE, &muted)) ++failures;
  if (device_ioctl(h, AWTRIX_PCM_STOP, NULL)) ++failures;
  h->carry_offset = h->carry_length = 0;
  h->core.muted = 1;
  return failures;
}

void ap_device_failed(ah_state *s) {
  ap_helper *h = (ap_helper *)s;
  struct awtrix_pcm_status status;
  ah_fail(s, TC002_AUDIO_ERROR_VENDOR,
          !read_status(h, &status) && status.hal_error ? status.hal_error : -EIO);
}
