#ifndef AWTRIX_TC002_FAKE_PCM_DEVICE_H
#define AWTRIX_TC002_FAKE_PCM_DEVICE_H

/* Host model of /dev/awtrix_pcm as the helper sees it, after src/platform/tc002/kmod/awtrix_pcm:
 * a 16 KiB DMA ring that keeps 256 bytes free, takes at most one 2048-byte period per step, starts
 * once 4096 bytes are prepared and then plays 88.2 bytes per millisecond. A ring that runs dry
 * while running stops the DMA; the next write counts an underrun and prepares a new stream.
 * Muted writes become zeros. STOP drops the ring. The clock is the caller's. */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include "platform/tc002/kmod/awtrix_pcm/awtrix_pcm.h"

enum {
  FAKE_PCM_BUFFER = 16384,
  FAKE_PCM_PERIOD = 2048,
  FAKE_PCM_START = 4096,
  FAKE_PCM_GUARD = 256,
  FAKE_PCM_SEGMENTS = 1024,
  FAKE_PCM_WRITE = 1
};

typedef struct fake_pcm {
  int64_t (*clock)(struct fake_pcm *);
  int64_t synced;
  uint32_t state, prepared_level, underruns;
  uint64_t written;
  int32_t gain_db, hal_error, last_gain_request;
  int muted, xrun, stuck;
  struct {
    double bytes;
    int sound;
  } segments[FAKE_PCM_SEGMENTS];
  unsigned segment_count;
  unsigned stops, drains, unmutes, mute_calls, gain_calls, writes, statuses, dry_runs;
  unsigned refuse;
  size_t accept_limit;
  /* The fail_after-th call of fail_request fails with fail_errno, and every later one if sticky. */
  unsigned fail_request, fail_after;
  int fail_errno, fail_sticky;
  int16_t *out;
  size_t out_capacity, out_count;
  unsigned long muted_sound, lost_sound, nonsilent_writes;
  unsigned order[64], order_count;
} fake_pcm;

static inline void fake_pcm_reset(fake_pcm *f, int64_t (*clock)(fake_pcm *), int16_t *out, size_t capacity) {
  memset(f, 0, sizeof *f);
  f->clock = clock;
  f->synced = clock(f);
  f->state = AWTRIX_PCM_PREPARED;
  f->gain_db = -28;
  f->out = out;
  f->out_capacity = capacity;
}

static inline double fake_pcm_level(const fake_pcm *f) {
  double total = 0;
  unsigned i;
  for (i = 0; i < f->segment_count; ++i) total += f->segments[i].bytes;
  return total;
}

static inline int fake_pcm_has_sound(const fake_pcm *f) {
  unsigned i;
  for (i = 0; i < f->segment_count; ++i)
    if (f->segments[i].sound && f->segments[i].bytes > 0) return 1;
  return 0;
}

static inline void fake_pcm_sync(fake_pcm *f) {
  const int64_t now = f->clock(f);
  double play = 88.2 * (double)(now - f->synced);
  f->synced = now;
  if (f->state != AWTRIX_PCM_RUNNING || f->stuck || play <= 0) return;
  while (play > 0 && f->segment_count) {
    double take = f->segments[0].bytes;
    if (take > play) take = play;
    f->segments[0].bytes -= take;
    play -= take;
    if (f->segments[0].bytes <= 1e-9) {
      memmove(f->segments, f->segments + 1, (f->segment_count - 1) * sizeof f->segments[0]);
      --f->segment_count;
    }
  }
  if (!f->segment_count && !f->xrun) {
    f->xrun = 1;
    ++f->dry_runs;
  }
}

static inline uint32_t fake_pcm_queued(const fake_pcm *f) { return (uint32_t)fake_pcm_level(f) & ~15u; }

static inline void fake_pcm_note(fake_pcm *f, unsigned what) {
  if (f->order_count < sizeof f->order / sizeof f->order[0]) f->order[f->order_count++] = what;
}

static inline void fake_pcm_prepare(fake_pcm *f) {
  unsigned i;
  for (i = 0; i < f->segment_count; ++i)
    if (f->segments[i].sound) f->lost_sound += (unsigned long)(f->segments[i].bytes / 2);
  f->segment_count = 0;
  f->state = AWTRIX_PCM_PREPARED;
  f->prepared_level = 0;
  f->xrun = 0;
}

static inline ssize_t fake_pcm_write(void *context, const void *data, size_t size) {
  fake_pcm *f = (fake_pcm *)context;
  const uint8_t *bytes = (const uint8_t *)data;
  size_t done = 0;
  fake_pcm_sync(f);
  fake_pcm_note(f, FAKE_PCM_WRITE);
  ++f->writes;
  if (size & 1) {
    errno = EINVAL;
    return -1;
  }
  if (f->state == AWTRIX_PCM_FAILED) {
    errno = EIO;
    return -1;
  }
  if (f->refuse) {
    --f->refuse;
    errno = EAGAIN;
    return -1;
  }
  while (done < size && f->segment_count < FAKE_PCM_SEGMENTS) {
    const double used = fake_pcm_level(f) + FAKE_PCM_GUARD;
    uint32_t room = used >= FAKE_PCM_BUFFER ? 0 : (uint32_t)(FAKE_PCM_BUFFER - used) & ~15u;
    size_t take = size - done, i;
    int sound = 0, raw_sound = 0;
    if (room > FAKE_PCM_PERIOD) room = FAKE_PCM_PERIOD;
    if (take > room) take = room;
    if (f->accept_limit && done + take > f->accept_limit) take = f->accept_limit - done;
    if (take < 16) break;
    if (f->state == AWTRIX_PCM_RUNNING && f->xrun) {
      ++f->underruns;
      fake_pcm_prepare(f);
    }
    for (i = 0; i < take / 2; ++i) {
      int16_t value;
      memcpy(&value, bytes + done + 2 * i, 2);
      if (value) raw_sound = 1;
      if (f->muted) value = 0;
      if (value) sound = 1;
      if (f->out && f->out_count < f->out_capacity) f->out[f->out_count++] = value;
    }
    if (raw_sound && f->muted) ++f->muted_sound;
    if (sound) ++f->nonsilent_writes;
    f->segments[f->segment_count].bytes = (double)take;
    f->segments[f->segment_count].sound = sound;
    ++f->segment_count;
    f->written += take;
    done += take;
    if (f->state == AWTRIX_PCM_PREPARED) {
      f->prepared_level += (uint32_t)take;
      if (f->prepared_level >= FAKE_PCM_START) {
        f->state = AWTRIX_PCM_RUNNING;
        f->xrun = 0;
      }
    }
  }
  if (!done) {
    errno = EAGAIN;
    return -1;
  }
  return (ssize_t)done;
}

static inline int fake_pcm_ioctl(void *context, unsigned request, void *argument) {
  fake_pcm *f = (fake_pcm *)context;
  fake_pcm_sync(f);
  fake_pcm_note(f, request);
  if (f->fail_request == request && f->fail_after) {
    if (f->fail_after > 1) {
      --f->fail_after;
    } else {
      if (!f->fail_sticky) f->fail_after = 0;
      errno = f->fail_errno;
      return -1;
    }
  }
  switch (request) {
    case AWTRIX_PCM_SET_GAIN: {
      int32_t db;
      memcpy(&db, argument, sizeof db);
      ++f->gain_calls;
      f->last_gain_request = db;
      f->gain_db = db < AWTRIX_PCM_MIN_GAIN_DB ? AWTRIX_PCM_MIN_GAIN_DB : db > -3 ? -3 : db;
      return 0;
    }
    case AWTRIX_PCM_SET_MUTE: {
      uint32_t muted;
      memcpy(&muted, argument, sizeof muted);
      ++f->mute_calls;
      if (!muted && f->muted) ++f->unmutes;
      f->muted = muted != 0;
      return 0;
    }
    case AWTRIX_PCM_DRAIN:
      ++f->drains;
      return 0;
    case AWTRIX_PCM_STOP:
      if (f->state == AWTRIX_PCM_FAILED) {
        errno = EIO;
        return -1;
      }
      ++f->stops;
      fake_pcm_prepare(f);
      return 0;
    case AWTRIX_PCM_GET_STATUS: {
      struct awtrix_pcm_status status;
      memset(&status, 0, sizeof status);
      ++f->statuses;
      status.state = f->state;
      status.buffer_bytes = FAKE_PCM_BUFFER;
      status.period_bytes = FAKE_PCM_PERIOD;
      status.queued_bytes = f->state == AWTRIX_PCM_FAILED ? 0 : fake_pcm_queued(f);
      status.written_bytes = f->written;
      status.underruns = f->underruns;
      status.gain_db = f->gain_db;
      status.muted = (uint32_t)f->muted;
      status.hal_error = f->hal_error;
      memcpy(argument, &status, sizeof status);
      return 0;
    }
    default:
      errno = ENOTTY;
      return -1;
  }
}

#endif
