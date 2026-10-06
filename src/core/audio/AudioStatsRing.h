#pragma once

#include <atomic>
#include <cstdint>

#include "core/audio/AudioStats.h"
#include "core/audio/StampedRing.h"

namespace awtrix {
namespace audio {

// Analysed frames stamped with when they come out of the speaker. One writer (the audio task),
// one reader (the render loop), no lock: each slot is a seqlock, and only 32-bit atomics are used
// because 64-bit ones are not lock-free on the ESP32.
class StatsRing {
 public:
  static constexpr int kSlots = 8;
  static constexpr int kStaleMs = 300;
  static constexpr int kInterestMs = 2000;

  void publish(const FrameStats& stats, int64_t audibleAtMs) {
    frames_.publish(stats, audibleAtMs);
  }

  // 0 is "nobody asked". A deadline that ran out is cleared here, on the audio task that asks every
  // block, so the 32-bit comparison never meets a deadline half the clock's range old.
  bool wanted(int64_t nowMs) {
    uint32_t until = wantedUntil_.load(std::memory_order_relaxed);
    if (until == 0) return false;
    if (static_cast<int32_t>(until - static_cast<uint32_t>(nowMs)) > 0) return true;
    wantedUntil_.compare_exchange_strong(until, 0, std::memory_order_relaxed);
    return false;
  }

  void markInterest(int64_t nowMs) {
    const uint32_t until = static_cast<uint32_t>(nowMs) + kInterestMs;
    wantedUntil_.store(until ? until : 1, std::memory_order_relaxed);
  }

  // The newest frame audible at nowMs. The reader state lives here, so it must be asked exactly
  // once per rendered frame: a beat is reported once, and beats in frames that came and went
  // between two calls are folded into the next answer.
  bool latestAudibleAt(int64_t nowMs, FrameStats& out) {
    const uint32_t head = frames_.head();
    bool found = false;
    uint32_t bestId = 0;
    int64_t bestAt = 0;
    FrameStats best;
    for (uint32_t offset = 0; offset < kSlots; ++offset) {
      const uint32_t id = head - offset;
      int64_t at;
      FrameStats st;
      if (!frames_.read(id, at, st)) continue;
      if (at <= nowMs) {
        found = true;
        bestId = id;
        bestAt = at;
        best = st;
        break;
      }
    }
    if (!found) return false;
    if (nowMs - bestAt > kStaleMs) {
      consumedId_ = bestId;
      return false;
    }
    bool beat = best.beat && bestId != consumedId_;
    for (uint32_t id = bestId - 1; static_cast<int32_t>(id - consumedId_) > 0 && head - id < kSlots; --id) {
      int64_t at;
      FrameStats st;
      if (frames_.read(id, at, st) && at >= nowMs - kStaleMs && st.beat)
        beat = true;
    }
    if (static_cast<int32_t>(bestId - consumedId_) > 0) consumedId_ = bestId;
    out = best;
    out.beat = beat;
    return true;
  }

 private:
  StampedRing<FrameStats, kSlots> frames_;
  std::atomic<uint32_t> wantedUntil_{0};
  uint32_t consumedId_ = 0;
};
static_assert(std::atomic<uint32_t>::is_always_lock_free, "the ring must never take a lock");

}
}
