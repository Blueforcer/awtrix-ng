#ifndef AWTRIX_TC002_AUDIO_PROTOCOL_H
#define AWTRIX_TC002_AUDIO_PROTOCOL_H

/* Messages between awtrix-linux and awtrix-tc002-audio-pcm. One message per SOCK_SEQPACKET
 * datagram: an 8-byte little-endian header {version, type, payload length, generation}
 * followed by the payload. Shared by the C helper and the C++ runtime. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../../core/sound/VolumeCurve.h"

enum {
  TC002_AUDIO_VERSION = 1,
  TC002_AUDIO_HEADER_BYTES = 8,
  TC002_AUDIO_MAX_PCM_BYTES = 4096,
  TC002_AUDIO_MAX_MESSAGE = TC002_AUDIO_HEADER_BYTES + TC002_AUDIO_MAX_PCM_BYTES,
  TC002_AUDIO_WINDOW_BYTES = 16384,
  TC002_AUDIO_DEVICE_RATE = 44100,
  TC002_AUDIO_MIN_RATE = 8000,
  TC002_AUDIO_MAX_RATE = 48000,
  TC002_AUDIO_MIN_DB = -60,
  TC002_AUDIO_MAX_DB = -3,
  TC002_AUDIO_OPEN_BYTES = 8,
  TC002_AUDIO_VOLUME_BYTES = 4,
  TC002_AUDIO_HELLO_BYTES = 16,
  TC002_AUDIO_STATUS_BYTES = 28
};

enum tc002_audio_type {
  TC002_AUDIO_OPEN = 1,
  TC002_AUDIO_PCM = 2,
  TC002_AUDIO_DRAIN = 3,
  TC002_AUDIO_STOP = 4,
  TC002_AUDIO_VOLUME = 5,
  TC002_AUDIO_HELLO = 16,
  TC002_AUDIO_STATUS = 17
};

enum tc002_audio_state {
  TC002_AUDIO_STARTING = 0,
  TC002_AUDIO_IDLE = 1,
  TC002_AUDIO_PRIMING = 2,
  TC002_AUDIO_PLAYING = 3,
  TC002_AUDIO_SETTLING = 4,
  TC002_AUDIO_PAUSED = 5,
  TC002_AUDIO_FAILED = 6
};

enum tc002_audio_error {
  TC002_AUDIO_ERROR_NONE = 0,
  TC002_AUDIO_ERROR_PREFLIGHT = 1,
  TC002_AUDIO_ERROR_VENDOR = 3,
  TC002_AUDIO_ERROR_QUEUE = 4,
  TC002_AUDIO_ERROR_PRIME_TIMEOUT = 5,
  TC002_AUDIO_ERROR_SETTLE_TIMEOUT = 6,
  TC002_AUDIO_ERROR_BAD_MESSAGE = 7,
  TC002_AUDIO_ERROR_OVERFLOW = 8
};

typedef struct {
  uint8_t type;
  uint16_t length;
  uint32_t generation;
  const uint8_t *payload;
} tc002_audio_frame;

typedef struct {
  uint32_t device_rate;
  uint8_t device_channels;
  int8_t min_db;
  int8_t max_db;
  uint32_t window_bytes;
  uint16_t max_pcm_bytes;
} tc002_audio_hello;

/* consumed_bytes counts PCM payload bytes of the header's generation that left the helper's
 * queue for the device; completed_generation is the newest generation that finished playing
 * out or was stopped. */
typedef struct {
  uint8_t state;
  uint8_t volume;
  uint8_t error;
  uint32_t consumed_bytes;
  uint32_t queued_bytes;
  uint32_t device_busy_bytes;
  uint32_t underruns;
  uint32_t completed_generation;
  int32_t vendor_result;
} tc002_audio_status;

/* The speaker gain in dB for a volume in percent: the volume curve of awtrix_volume_db() clamped
 * to the device's range. The helper sets it; the runtime's mixer scales quieter layers by the
 * difference. */
static inline int8_t tc002_audio_volume_db(uint8_t percent) {
  const int8_t db = awtrix_volume_db(percent);
  if (db < TC002_AUDIO_MIN_DB) return TC002_AUDIO_MIN_DB;
  if (db > TC002_AUDIO_MAX_DB) return TC002_AUDIO_MAX_DB;
  return db;
}

static inline void tc002_audio_put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static inline void tc002_audio_put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static inline uint16_t tc002_audio_get16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t tc002_audio_get32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline size_t tc002_audio_header(uint8_t *out, size_t capacity, uint8_t type,
                                        uint32_t generation, size_t payload) {
  if (!out || payload > TC002_AUDIO_MAX_PCM_BYTES ||
      capacity < TC002_AUDIO_HEADER_BYTES + payload)
    return 0;
  out[0] = TC002_AUDIO_VERSION;
  out[1] = type;
  tc002_audio_put16(out + 2, (uint16_t)payload);
  tc002_audio_put32(out + 4, generation);
  return TC002_AUDIO_HEADER_BYTES + payload;
}

static inline size_t tc002_audio_encode_open(uint8_t *out, size_t capacity, uint32_t generation,
                                             uint32_t rate, uint8_t channels) {
  const size_t n = tc002_audio_header(out, capacity, TC002_AUDIO_OPEN, generation,
                                      TC002_AUDIO_OPEN_BYTES);
  if (!n) return 0;
  memset(out + TC002_AUDIO_HEADER_BYTES, 0, TC002_AUDIO_OPEN_BYTES);
  tc002_audio_put32(out + 8, rate);
  out[12] = channels;
  return n;
}

static inline size_t tc002_audio_encode_pcm(uint8_t *out, size_t capacity, uint32_t generation,
                                            const int16_t *samples, size_t count) {
  size_t i, n;
  if (!samples || !count || count * 2 > TC002_AUDIO_MAX_PCM_BYTES) return 0;
  n = tc002_audio_header(out, capacity, TC002_AUDIO_PCM, generation, count * 2);
  if (!n) return 0;
  for (i = 0; i < count; ++i)
    tc002_audio_put16(out + TC002_AUDIO_HEADER_BYTES + 2 * i, (uint16_t)samples[i]);
  return n;
}

static inline size_t tc002_audio_encode_empty(uint8_t *out, size_t capacity, uint8_t type,
                                              uint32_t generation) {
  if (type != TC002_AUDIO_DRAIN && type != TC002_AUDIO_STOP) return 0;
  return tc002_audio_header(out, capacity, type, generation, 0);
}

static inline size_t tc002_audio_encode_volume(uint8_t *out, size_t capacity, uint8_t percent) {
  const size_t n = tc002_audio_header(out, capacity, TC002_AUDIO_VOLUME, 0, TC002_AUDIO_VOLUME_BYTES);
  if (!n || percent > 100) return 0;
  memset(out + TC002_AUDIO_HEADER_BYTES, 0, TC002_AUDIO_VOLUME_BYTES);
  out[8] = percent;
  return n;
}

static inline size_t tc002_audio_encode_hello(uint8_t *out, size_t capacity,
                                              const tc002_audio_hello *hello) {
  const size_t n = tc002_audio_header(out, capacity, TC002_AUDIO_HELLO, 0, TC002_AUDIO_HELLO_BYTES);
  if (!n || !hello) return 0;
  memset(out + TC002_AUDIO_HEADER_BYTES, 0, TC002_AUDIO_HELLO_BYTES);
  tc002_audio_put32(out + 8, hello->device_rate);
  out[12] = hello->device_channels;
  out[13] = (uint8_t)hello->min_db;
  out[14] = (uint8_t)hello->max_db;
  tc002_audio_put32(out + 16, hello->window_bytes);
  tc002_audio_put16(out + 20, hello->max_pcm_bytes);
  return n;
}

static inline size_t tc002_audio_encode_status(uint8_t *out, size_t capacity, uint32_t generation,
                                               const tc002_audio_status *status) {
  const size_t n = tc002_audio_header(out, capacity, TC002_AUDIO_STATUS, generation,
                                      TC002_AUDIO_STATUS_BYTES);
  if (!n || !status) return 0;
  memset(out + TC002_AUDIO_HEADER_BYTES, 0, TC002_AUDIO_STATUS_BYTES);
  out[8] = status->state;
  out[9] = status->volume;
  out[10] = status->error;
  tc002_audio_put32(out + 12, status->consumed_bytes);
  tc002_audio_put32(out + 16, status->queued_bytes);
  tc002_audio_put32(out + 20, status->device_busy_bytes);
  tc002_audio_put32(out + 24, status->underruns);
  tc002_audio_put32(out + 28, status->completed_generation);
  tc002_audio_put32(out + 32, (uint32_t)status->vendor_result);
  return n;
}

/* Checks the header and the payload length each type requires; PCM is further checked
 * against the open channel count by the helper. */
static inline int tc002_audio_parse(const uint8_t *data, size_t size, tc002_audio_frame *out) {
  uint16_t length;
  if (!data || !out || size < TC002_AUDIO_HEADER_BYTES || data[0] != TC002_AUDIO_VERSION) return 0;
  length = tc002_audio_get16(data + 2);
  if ((size_t)length != size - TC002_AUDIO_HEADER_BYTES) return 0;
  switch (data[1]) {
    case TC002_AUDIO_OPEN: if (length != TC002_AUDIO_OPEN_BYTES) return 0; break;
    case TC002_AUDIO_PCM:
      if (!length || length > TC002_AUDIO_MAX_PCM_BYTES || (length & 1)) return 0;
      break;
    case TC002_AUDIO_DRAIN:
    case TC002_AUDIO_STOP: if (length) return 0; break;
    case TC002_AUDIO_VOLUME: if (length != TC002_AUDIO_VOLUME_BYTES) return 0; break;
    case TC002_AUDIO_HELLO: if (length != TC002_AUDIO_HELLO_BYTES) return 0; break;
    case TC002_AUDIO_STATUS: if (length != TC002_AUDIO_STATUS_BYTES) return 0; break;
    default: return 0;
  }
  out->type = data[1];
  out->length = length;
  out->generation = tc002_audio_get32(data + 4);
  out->payload = data + TC002_AUDIO_HEADER_BYTES;
  return 1;
}

static inline int tc002_audio_read_open(const tc002_audio_frame *frame, uint32_t *rate,
                                        uint8_t *channels) {
  uint32_t r;
  uint8_t c;
  if (!frame || frame->type != TC002_AUDIO_OPEN || !frame->generation) return 0;
  r = tc002_audio_get32(frame->payload);
  c = frame->payload[4];
  if (r < TC002_AUDIO_MIN_RATE || r > TC002_AUDIO_MAX_RATE || c < 1 || c > 2) return 0;
  *rate = r;
  *channels = c;
  return 1;
}

static inline int tc002_audio_read_volume(const tc002_audio_frame *frame, uint8_t *percent) {
  if (!frame || frame->type != TC002_AUDIO_VOLUME || frame->payload[0] > 100) return 0;
  *percent = frame->payload[0];
  return 1;
}

static inline int tc002_audio_read_hello(const tc002_audio_frame *frame, tc002_audio_hello *out) {
  if (!frame || frame->type != TC002_AUDIO_HELLO) return 0;
  out->device_rate = tc002_audio_get32(frame->payload);
  out->device_channels = frame->payload[4];
  out->min_db = (int8_t)frame->payload[5];
  out->max_db = (int8_t)frame->payload[6];
  out->window_bytes = tc002_audio_get32(frame->payload + 8);
  out->max_pcm_bytes = tc002_audio_get16(frame->payload + 12);
  return out->window_bytes >= TC002_AUDIO_MAX_PCM_BYTES && out->max_pcm_bytes >= 2 &&
         out->max_pcm_bytes <= TC002_AUDIO_MAX_PCM_BYTES;
}

static inline int tc002_audio_read_status(const tc002_audio_frame *frame, tc002_audio_status *out) {
  if (!frame || frame->type != TC002_AUDIO_STATUS) return 0;
  out->state = frame->payload[0];
  out->volume = frame->payload[1];
  out->error = frame->payload[2];
  out->consumed_bytes = tc002_audio_get32(frame->payload + 4);
  out->queued_bytes = tc002_audio_get32(frame->payload + 8);
  out->device_busy_bytes = tc002_audio_get32(frame->payload + 12);
  out->underruns = tc002_audio_get32(frame->payload + 16);
  out->completed_generation = tc002_audio_get32(frame->payload + 20);
  out->vendor_result = (int32_t)tc002_audio_get32(frame->payload + 24);
  return out->state <= TC002_AUDIO_FAILED;
}

#endif
