#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace awtrix::render {

struct TimingSummary {
  uint32_t count = 0, meanUs = 0, p50Us = 0, p95Us = 0, p99Us = 0, maxUs = 0;
};

// Quantiles use the most recent 64 samples; count, mean and maximum cover the whole window.
class TimingSamples {
 public:
  void add(uint32_t microseconds) {
    recent_[count_ % recent_.size()] = microseconds;
    ++count_;
    total_ += microseconds;
    maximum_ = std::max(maximum_, microseconds);
  }
  TimingSummary snapshot() const {
    if (!count_) return {};
    auto ordered = recent_;
    const std::size_t n = std::min<std::size_t>(count_, ordered.size());
    std::sort(ordered.begin(), ordered.begin() + n);
    auto percentile = [&](std::size_t percent) { return ordered[(n * percent + 99) / 100 - 1]; };
    return {count_, static_cast<uint32_t>(total_ / count_), percentile(50),
            percentile(95), percentile(99), maximum_};
  }
 private:
  std::array<uint32_t, 64> recent_{};
  uint32_t count_ = 0;
  uint64_t total_ = 0;
  uint32_t maximum_ = 0;
};

enum class FramePhase : uint8_t { Services, Tick, Render, Mapping, Output, Interval, Count };

class FrameDiagnostics {
 public:
  void record(FramePhase phase, uint32_t microseconds) {
    samples_[static_cast<std::size_t>(phase)].add(microseconds);
  }
  TimingSummary snapshot(FramePhase phase) const {
    return samples_[static_cast<std::size_t>(phase)].snapshot();
  }
  void rendered(int64_t atUs) { rendered_.add(atUs); }
  void presented(int64_t atUs) {
    if (presented_.count && atUs >= presented_.last)
      record(FramePhase::Interval, static_cast<uint32_t>(atUs - presented_.last));
    presented_.add(atUs);
  }
  uint32_t renderFpsMilli() const { return rendered_.rate(); }
  uint32_t presentationFpsMilli() const { return presented_.rate(); }
 private:
  struct Rate {
    uint32_t count = 0;
    int64_t first = 0, last = 0;
    void add(int64_t now) {
      if (!count || now < last) { first = now; count = 0; }
      last = now;
      ++count;
    }
    uint32_t rate() const {
      return count > 1 && last > first ? static_cast<uint32_t>(
          static_cast<uint64_t>(count - 1) * 1000000000ULL / (last - first)) : 0;
    }
  };
  std::array<TimingSamples, static_cast<std::size_t>(FramePhase::Count)> samples_;
  Rate rendered_, presented_;
};

}
