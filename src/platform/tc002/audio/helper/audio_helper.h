#ifndef AWTRIX_TC002_AUDIO_HELPER_H
#define AWTRIX_TC002_AUDIO_HELPER_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "../../contract/AudioProtocol.h"

enum {
  AH_BLOCK_SAMPLES = 1024,
  AH_BLOCK_BYTES = 2048,
  AH_TARGET_BUSY_BYTES = 8192,
  AH_LOW_BUSY_BYTES = 2048,
  AH_PRIME_MS = 500,
  AH_SETTLE_MS = 1000,
  AH_PAUSE_AFTER_MS = 1000,
  AH_TICK_MS = 10,
  AH_REFUSAL_LIMIT = 100,
  AH_QUEUE_SAMPLES = TC002_AUDIO_WINDOW_BYTES / 2
};
/* What AH_SEND returns when the device has no room for the block. */
#define AH_DEVICE_FULL (-EAGAIN)
#ifdef __cplusplus
#define AH_ALIGN8 alignas(8)
#else
#define AH_ALIGN8 _Alignas(8)
#endif

enum ah_call { AH_SET_MUTE, AH_SET_VOLUME, AH_CLEAR, AH_QUERY, AH_SEND };

/* send returns 1 when the datagram went out, 0 when the socket is full, -1 when it broke. */
typedef struct {
  void *context;
  int64_t (*now_ms)(void *);
  int32_t (*call)(void *, enum ah_call, void *, int32_t);
  int (*send)(void *, const uint8_t *, size_t);
} ah_ops;

enum ah_settle_next { AH_SETTLE_TO_IDLE, AH_SETTLE_TO_PAUSE };

typedef struct {
  const ah_ops *ops;
  uint8_t state, error, volume;
  int32_t vendor_result;
  int muted;
  int hello_pending, status_dirty, broken;
  uint32_t generation, completed, rate, channels;
  int draining, silence_sent, starving;
  enum ah_settle_next settle_next;
  /* Device-busy levels that end priming and settling, and whether settling always flushes. */
  uint32_t primed_busy, settled_busy;
  int clear_after_settle;
  int64_t deadline, starved_since;
  uint32_t step, phase;
  unsigned head, count;
  uint32_t consumed, busy, underruns;
  unsigned refusals;
  int16_t queue[AH_QUEUE_SAMPLES];
  AH_ALIGN8 int16_t block[AH_BLOCK_SAMPLES];
} ah_state;

int8_t ah_volume_db(uint8_t percent);

/* Resets s for a start over ops: muted and STARTING. */
void ah_init(ah_state *s, const ah_ops *ops);
void ah_receive(ah_state *s, const uint8_t *data, size_t size, int64_t now);
/* Returns how long the caller may wait for the socket before calling again; -1 is forever. */
int ah_service(ah_state *s, int64_t now);
void ah_flush(ah_state *s);
void ah_fail(ah_state *s, uint8_t error, int32_t vendor_result);

#endif
