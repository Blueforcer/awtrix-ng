#pragma once

#include <atomic>
#include <cstdint>

#include "core/audio/AudioStats.h"

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
    const uint32_t id = head_.load(std::memory_order_relaxed) + 1;
    Slot& s = slots_[id % kSlots];
    const uint32_t v = s.seq.load(std::memory_order_relaxed);
    s.seq.store(v + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    s.id = id;
    s.audibleAtMs = audibleAtMs;
    s.stats = stats;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    s.seq.store(v + 2, std::memory_order_relaxed);
    head_.store(id, std::memory_order_release);
  }

  bool wanted(int64_t nowMs) const {
    const uint32_t until = wantedUntil_.load(std::memory_order_relaxed);
    return static_cast<int32_t>(until - static_cast<uint32_t>(nowMs)) > 0;
  }

  void markInterest(int64_t nowMs) {
    wantedUntil_.store(static_cast<uint32_t>(nowMs) + kInterestMs, std::memory_order_relaxed);
  }

  // The newest frame audible at nowMs. The reader state lives here, so it must be asked exactly
  // once per rendered frame: a beat is reported once, and beats in frames that came and went
  // between two calls are folded into the next answer.
  bool latestAudibleAt(int64_t nowMs, FrameStats& out) {
    const uint32_t head = head_.load(std::memory_order_acquire);
    if (head == 0) return false;
    uint32_t bestId = 0;
    int64_t bestAt = 0;
    FrameStats best;
    for (uint32_t id = head; id > 0 && id + kSlots > head; --id) {
      uint32_t rid;
      int64_t at;
      FrameStats st;
      if (!read(slots_[id % kSlots], rid, at, st) || rid != id) continue;
      if (at <= nowMs) {
        bestId = id;
        bestAt = at;
        best = st;
        break;
      }
    }
    if (bestId == 0) return false;
    if (nowMs - bestAt > kStaleMs) {
      consumedId_ = bestId;
      return false;
    }
    bool beat = best.beat && bestId != consumedId_;
    for (uint32_t id = bestId - 1; id > consumedId_ && id + kSlots > head; --id) {
      uint32_t rid;
      int64_t at;
      FrameStats st;
      if (read(slots_[id % kSlots], rid, at, st) && rid == id && at >= nowMs - kStaleMs && st.beat)
        beat = true;
    }
    if (bestId > consumedId_) consumedId_ = bestId;
    out = best;
    out.beat = beat;
    return true;
  }

 private:
  struct Slot {
    std::atomic<uint32_t> seq{0};
    uint32_t id = 0;
    int64_t audibleAtMs = 0;
    FrameStats stats;
  };

  static bool read(const Slot& s, uint32_t& id, int64_t& at, FrameStats& st) {
    const uint32_t v1 = s.seq.load(std::memory_order_relaxed);
    if (v1 & 1) return false;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    id = s.id;
    at = s.audibleAtMs;
    st = s.stats;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return s.seq.load(std::memory_order_relaxed) == v1;
  }

  Slot slots_[kSlots];
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> wantedUntil_{0};
  uint32_t consumedId_ = 0;
};
static_assert(std::atomic<uint32_t>::is_always_lock_free, "the ring must never take a lock");

}
}
