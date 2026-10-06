#include "audio_helper.h"

#include <stddef.h>
#include <string.h>

int8_t ah_volume_db(uint8_t percent) { return tc002_audio_volume_db(percent); }

static void dirty(ah_state *s) { s->status_dirty = 1; }

void ah_fail(ah_state *s, uint8_t error, int32_t vendor_result) {
  if (s->state != TC002_AUDIO_FAILED) {
    s->error = error;
    s->vendor_result = vendor_result;
  }
  s->state = TC002_AUDIO_FAILED;
  dirty(s);
}

static int32_t call(ah_state *s, enum ah_call which, void *data, int32_t value) {
  return s->ops->call(s->ops->context, which, data, value);
}

static int require_zero(ah_state *s, enum ah_call which, void *data, int32_t value) {
  const int32_t result = call(s, which, data, value);
  if (result) ah_fail(s, TC002_AUDIO_ERROR_VENDOR, result);
  return !result;
}

static int query(ah_state *s) {
  uint32_t q[3] = {0, 0, 0};
  const int32_t result = call(s, AH_QUERY, q, 0);
  if (result) {
    ah_fail(s, TC002_AUDIO_ERROR_VENDOR, result);
    return 0;
  }
  if (!q[0] || ((q[0] | q[1] | q[2]) & 15u) || q[1] > q[0] || q[2] > q[0] || q[1] != q[0] - q[2]) {
    ah_fail(s, TC002_AUDIO_ERROR_QUEUE, 0);
    return 0;
  }
  s->busy = q[2];
  return 1;
}

static int set_mute(ah_state *s, int muted) {
  if (!require_zero(s, AH_SET_MUTE, NULL, muted ? 1 : 0)) return 0;
  s->muted = muted;
  return 1;
}

/* 1 accepted, 0 buffer full, -1 failed. */
static int send_block(ah_state *s) {
  const int32_t result = call(s, AH_SEND, s->block, AH_BLOCK_BYTES);
  if (!result) {
    s->busy += AH_BLOCK_BYTES;
    return 1;
  }
  if (result == AH_DEVICE_FULL) return 0;
  ah_fail(s, TC002_AUDIO_ERROR_VENDOR, result);
  return -1;
}

static int send_silence(ah_state *s) {
  memset(s->block, 0, sizeof s->block);
  return send_block(s);
}

static void clear_queue(ah_state *s) {
  s->head = 0;
  s->count = 0;
  s->phase = 0;
}

static int16_t queued(const ah_state *s, unsigned index) {
  return s->queue[(s->head + index) % AH_QUEUE_SAMPLES];
}

static void consume(ah_state *s, unsigned frames) {
  if (frames > s->count) frames = s->count;
  s->head = (s->head + frames) % AH_QUEUE_SAMPLES;
  s->count -= frames;
  s->consumed += frames * 2u * s->channels;
  dirty(s);
}

static unsigned frames_needed(const ah_state *s) {
  const uint64_t last = (uint64_t)s->phase + (uint64_t)(AH_BLOCK_SAMPLES - 1) * s->step;
  return (unsigned)(last >> 16) + ((last & 0xffffu) ? 2u : 1u);
}

/* Linear interpolation from the queued mono samples at the open rate to 44.1 kHz. The queue
 * only advances once the device accepted the block. */
static int send_audio_block(ah_state *s) {
  unsigned i;
  int sent;
  uint64_t position = s->phase;
  for (i = 0; i < AH_BLOCK_SAMPLES; ++i, position += s->step) {
    const unsigned index = (unsigned)(position >> 16);
    const int32_t fraction = (int32_t)(position & 0xffffu);
    int32_t a, b;
    if (index >= s->count) {
      s->block[i] = 0;
      continue;
    }
    a = queued(s, index);
    b = fraction && index + 1 < s->count ? queued(s, index + 1) : a;
    s->block[i] = (int16_t)(a + (int32_t)(((int64_t)(b - a) * fraction) / 65536));
  }
  sent = send_block(s);
  if (sent <= 0) return sent;
  s->phase = (uint32_t)(position & 0xffffu);
  consume(s, (unsigned)(position >> 16));
  return 1;
}

static void set_state(ah_state *s, uint8_t state) {
  if (s->state != state) dirty(s);
  s->state = state;
}

static void begin_priming(ah_state *s, int64_t now) {
  set_state(s, TC002_AUDIO_PRIMING);
  s->silence_sent = 0;
  s->deadline = now + AH_PRIME_MS;
}

static void begin_settling(ah_state *s, int64_t now, enum ah_settle_next next) {
  set_state(s, TC002_AUDIO_SETTLING);
  s->settle_next = next;
  s->silence_sent = 0;
  s->deadline = now + AH_SETTLE_MS;
}

static void complete_generation(ah_state *s) {
  if (s->generation) s->completed = s->generation;
  s->generation = 0;
  s->draining = 0;
  s->starving = 0;
  clear_queue(s);
  set_state(s, TC002_AUDIO_IDLE);
  dirty(s);
}

/* Mute before the flush so whatever the DMA still holds is never heard. */
static void stop_generation(ah_state *s) {
  if (!s->muted && !set_mute(s, 1)) return;
  if (!require_zero(s, AH_CLEAR, NULL, 0)) return;
  s->busy = 0;
  complete_generation(s);
}

static int unmute_if_audible(ah_state *s) {
  if (!s->volume || !s->muted) return 1;
  return set_mute(s, 0);
}

void ah_init(ah_state *s, const ah_ops *ops) {
  memset(s, 0, sizeof *s);
  s->ops = ops;
  s->muted = 1;
  s->state = TC002_AUDIO_STARTING;
  s->status_dirty = 1;
  s->primed_busy = UINT32_MAX;
  s->settled_busy = AH_BLOCK_BYTES;
  s->clear_after_settle = 1;
}

static void receive_pcm(ah_state *s, const tc002_audio_frame *frame) {
  const unsigned width = 2u * s->channels;
  unsigned frames, i;
  if (!s->generation || frame->generation != s->generation) return;
  if (frame->length % width) {
    s->error = TC002_AUDIO_ERROR_BAD_MESSAGE;
    dirty(s);
    return;
  }
  frames = frame->length / width;
  if (s->draining) {
    s->error = TC002_AUDIO_ERROR_BAD_MESSAGE;
    dirty(s);
    return;
  }
  if (frames > AH_QUEUE_SAMPLES - s->count ||
      (s->count + frames) * width > TC002_AUDIO_WINDOW_BYTES) {
    s->error = TC002_AUDIO_ERROR_OVERFLOW;
    dirty(s);
    return;
  }
  for (i = 0; i < frames; ++i) {
    const uint8_t *p = frame->payload + i * width;
    int32_t value = (int16_t)tc002_audio_get16(p);
    if (s->channels == 2) value = (value + (int16_t)tc002_audio_get16(p + 2)) / 2;
    s->queue[(s->head + s->count) % AH_QUEUE_SAMPLES] = (int16_t)value;
    ++s->count;
  }
  dirty(s);
}

static void apply_volume(ah_state *s, uint8_t percent) {
  s->volume = percent;
  dirty(s);
  if (!require_zero(s, AH_SET_VOLUME, NULL, ah_volume_db(percent))) return;
  if (s->state != TC002_AUDIO_PLAYING) return;
  if (!percent && !s->muted) set_mute(s, 1);
  else if (percent && s->muted) set_mute(s, 0);
}

void ah_receive(ah_state *s, const uint8_t *data, size_t size, int64_t now) {
  tc002_audio_frame frame;
  uint32_t rate;
  uint8_t channels, percent;
  if (s->state == TC002_AUDIO_FAILED || s->state == TC002_AUDIO_STARTING) return;
  if (!tc002_audio_parse(data, size, &frame)) {
    s->error = TC002_AUDIO_ERROR_BAD_MESSAGE;
    dirty(s);
    return;
  }
  switch (frame.type) {
    case TC002_AUDIO_OPEN:
      if (!tc002_audio_read_open(&frame, &rate, &channels)) {
        s->error = TC002_AUDIO_ERROR_BAD_MESSAGE;
        dirty(s);
        return;
      }
      if (s->generation) stop_generation(s);
      if (s->state == TC002_AUDIO_FAILED) return;
      s->generation = frame.generation;
      s->rate = rate;
      s->channels = channels;
      s->step = (uint32_t)(((uint64_t)rate << 16) / TC002_AUDIO_DEVICE_RATE);
      s->consumed = 0;
      s->draining = 0;
      s->starving = 0;
      clear_queue(s);
      begin_priming(s, now);
      dirty(s);
      return;
    case TC002_AUDIO_PCM: receive_pcm(s, &frame); return;
    case TC002_AUDIO_DRAIN:
      if (s->generation && frame.generation == s->generation) {
        s->draining = 1;
        dirty(s);
      }
      return;
    case TC002_AUDIO_STOP:
      if (s->generation && frame.generation == s->generation) stop_generation(s);
      dirty(s);
      return;
    case TC002_AUDIO_VOLUME:
      if (tc002_audio_read_volume(&frame, &percent)) apply_volume(s, percent);
      else {
        s->error = TC002_AUDIO_ERROR_BAD_MESSAGE;
        dirty(s);
      }
      return;
    default:
      s->error = TC002_AUDIO_ERROR_BAD_MESSAGE;
      dirty(s);
      return;
  }
}

static int service_priming(ah_state *s, int64_t now) {
  if (!s->silence_sent) {
    const int sent = send_silence(s);
    if (sent < 0) return -1;
    if (sent) s->silence_sent = 1;
    else if (now >= s->deadline) {
      s->error = TC002_AUDIO_ERROR_PRIME_TIMEOUT;
      stop_generation(s);
      return -1;
    }
    return AH_TICK_MS;
  }
  if (!query(s)) return -1;
  if (s->busy <= s->primed_busy) {
    if (!unmute_if_audible(s)) return -1;
    set_state(s, TC002_AUDIO_PLAYING);
    return 0;
  }
  if (now >= s->deadline) {
    s->error = TC002_AUDIO_ERROR_PRIME_TIMEOUT;
    stop_generation(s);
    return -1;
  }
  return AH_TICK_MS;
}

static int service_settling(ah_state *s, int64_t now) {
  int timed_out = 0;
  if (!s->silence_sent) {
    const int sent = send_silence(s);
    if (sent < 0) return -1;
    if (sent) s->silence_sent = 1;
    else if (now >= s->deadline) timed_out = 1;
    if (!timed_out) return AH_TICK_MS;
  }
  if (!timed_out) {
    if (!query(s)) return -1;
    if (s->busy > s->settled_busy && now < s->deadline) return AH_TICK_MS;
    timed_out = s->busy > s->settled_busy;
  }
  if (!s->muted && !set_mute(s, 1)) return -1;
  if (timed_out) s->error = TC002_AUDIO_ERROR_SETTLE_TIMEOUT;
  if (timed_out || s->clear_after_settle) {
    if (!require_zero(s, AH_CLEAR, NULL, 0)) return -1;
    s->busy = 0;
  }
  if (s->settle_next == AH_SETTLE_TO_IDLE) complete_generation(s);
  else set_state(s, TC002_AUDIO_PAUSED);
  return 0;
}

static int service_playing(ah_state *s, int64_t now) {
  if (!query(s)) return -1;
  while (s->busy + AH_BLOCK_BYTES <= AH_TARGET_BUSY_BYTES) {
    int sent;
    if (s->count >= frames_needed(s) || (s->draining && s->count)) {
      sent = send_audio_block(s);
      if (sent < 0) return -1;
      if (!sent) {
        if (++s->refusals >= AH_REFUSAL_LIMIT) {
          ah_fail(s, TC002_AUDIO_ERROR_VENDOR, AH_DEVICE_FULL);
          return -1;
        }
        return AH_TICK_MS;
      }
      s->refusals = 0;
      s->starving = 0;
      continue;
    }
    if (s->draining) {
      begin_settling(s, now, AH_SETTLE_TO_IDLE);
      return service_settling(s, now);
    }
    if (s->busy >= AH_LOW_BUSY_BYTES) break;
    if (!s->starving) {
      s->starving = 1;
      s->starved_since = now;
      ++s->underruns;
      dirty(s);
    }
    if (now - s->starved_since >= AH_PAUSE_AFTER_MS) {
      begin_settling(s, now, AH_SETTLE_TO_PAUSE);
      return service_settling(s, now);
    }
    if (send_silence(s) < 0) return -1;
    break;
  }
  return AH_TICK_MS;
}

int ah_service(ah_state *s, int64_t now) {
  int wait = 0;
  unsigned rounds;
  for (rounds = 0; rounds < 4 && wait == 0; ++rounds) {
    switch (s->state) {
      case TC002_AUDIO_PAUSED:
        if (s->draining && !s->count) {
          complete_generation(s);
          return -1;
        }
        if (s->count < frames_needed(s) && !(s->draining && s->count)) return -1;
        begin_priming(s, now);
        wait = 0;
        break;
      case TC002_AUDIO_PRIMING: wait = service_priming(s, now); break;
      case TC002_AUDIO_PLAYING: wait = service_playing(s, now); break;
      case TC002_AUDIO_SETTLING: wait = service_settling(s, now); break;
      default: return -1;
    }
  }
  return wait == 0 ? AH_TICK_MS : wait;
}

void ah_flush(ah_state *s) {
  uint8_t message[TC002_AUDIO_MAX_MESSAGE];
  size_t size;
  int sent;
  if (s->broken) return;
  if (s->hello_pending) {
    tc002_audio_hello hello;
    hello.device_rate = TC002_AUDIO_DEVICE_RATE;
    hello.device_channels = 1;
    hello.min_db = TC002_AUDIO_MIN_DB;
    hello.max_db = TC002_AUDIO_MAX_DB;
    hello.window_bytes = TC002_AUDIO_WINDOW_BYTES;
    hello.max_pcm_bytes = TC002_AUDIO_MAX_PCM_BYTES;
    size = tc002_audio_encode_hello(message, sizeof message, &hello);
    sent = s->ops->send(s->ops->context, message, size);
    if (sent < 0) s->broken = 1;
    if (sent <= 0) return;
    s->hello_pending = 0;
  }
  if (!s->status_dirty) return;
  {
    tc002_audio_status status;
    status.state = s->state;
    status.volume = s->volume;
    status.error = s->error;
    status.consumed_bytes = s->consumed;
    status.queued_bytes = s->count * 2u * (s->channels ? s->channels : 1u);
    status.device_busy_bytes = s->busy;
    status.underruns = s->underruns;
    status.completed_generation = s->completed;
    status.vendor_result = s->vendor_result;
    size = tc002_audio_encode_status(message, sizeof message, s->generation, &status);
  }
  sent = s->ops->send(s->ops->context, message, size);
  if (sent < 0) s->broken = 1;
  if (sent > 0) s->status_dirty = 0;
}
