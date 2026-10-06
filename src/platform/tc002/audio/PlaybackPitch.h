#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/audio/PitchDetector.h"
#include "core/audio/StampedRing.h"

namespace awtrix {
namespace tc002 {

// The pitch of what the speaker plays. The audio thread feeds every decoded block; while the render
// thread has asked within kDemandMs it folds them to mono at no more than the detector's rate and
// detects one window at a time, stamped with when its first sample is heard. The render thread
// reads the window heard now. One writer, one reader, no lock: each slot is a seqlock, as in
// audio::StatsRing.
class PlaybackPitch {
 public:
  static constexpr int kWindow = 1024;
  static constexpr int kSlots = 16;
  static constexpr int64_t kStaleMs = 300;
  static constexpr int64_t kDemandMs = 1000;

  // Render thread. True while a window was heard within kStaleMs; hz is its pitch, 0 for none.
  // Asking is what keeps the audio thread detecting.
  bool pitch(int64_t nowMs, float& hz) {
    wantedUntil_.store(nowMs + kDemandMs, std::memory_order_relaxed);
    hz = 0.f;
    const uint32_t head = pitches_.head();
    for (uint32_t offset = 0; offset < kSlots; ++offset) {
      const uint32_t id = head - offset;
      int64_t at;
      float value;
      if (!pitches_.read(id, at, value) || at > nowMs) continue;
      if (nowMs - at > kStaleMs) return false;
      hz = value;
      return true;
    }
    return false;
  }

  // Audio thread: one decoded block, interleaved, whose first frame is heard at audibleAtMs.
  void feed(const int16_t* samples, std::size_t frames, uint8_t channels, uint32_t rate, int64_t nowMs,
            int64_t audibleAtMs) {
    if (nowMs >= wantedUntil_.load(std::memory_order_relaxed) || !samples || !channels || !rate) {
      fill_ = 0;
      folded_ = 0;
      return;
    }
    if (rate != rate_ || channels != channels_) {
      rate_ = rate;
      channels_ = channels;
      factor_ = (rate + audio::PitchDetector::kMaxRateHz - 1) / audio::PitchDetector::kMaxRateHz;
      fill_ = 0;
      folded_ = 0;
    }
    const int32_t divisor = static_cast<int32_t>(factor_ * channels);
    for (std::size_t i = 0; i < frames; ++i) {
      if (fill_ == 0 && folded_ == 0)
        windowAt_ = audibleAtMs + static_cast<int64_t>(i) * 1000 / static_cast<int64_t>(rate);
      if (folded_ == 0) sum_ = 0;
      for (uint8_t c = 0; c < channels; ++c) sum_ += samples[i * channels + c];
      if (++folded_ < factor_) continue;
      folded_ = 0;
      window_[fill_] = static_cast<int16_t>(sum_ / divisor);
      if (++fill_ < kWindow) continue;
      fill_ = 0;
      pitches_.publish(detector_.detect(window_.data(), kWindow, static_cast<int>(rate / factor_)), windowAt_);
    }
  }

 private:
  audio::StampedRing<float, kSlots> pitches_;
  std::atomic<int64_t> wantedUntil_{-1};

  audio::PitchDetector detector_;
  std::array<int16_t, kWindow> window_{};
  uint32_t rate_ = 0;
  uint8_t channels_ = 0;
  uint32_t factor_ = 1;
  uint32_t folded_ = 0;
  int fill_ = 0;
  int32_t sum_ = 0;
  int64_t windowAt_ = 0;
};

}
}
