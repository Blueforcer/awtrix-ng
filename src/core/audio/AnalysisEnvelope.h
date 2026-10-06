#pragma once

#include <algorithm>
#include <cmath>

#include "core/audio/AudioStats.h"

namespace awtrix::audio {

// Display envelope for analysis values, independent of PCM source and render rate.
class AnalysisEnvelope {
 public:
  FrameStats process(const FrameStats& target, int64_t nowMs) {
    const bool first = at_ < 0 || nowMs < at_;
    const auto elapsed = first ? 0 : nowMs - at_;
    const float rise = 1.f - std::exp(-static_cast<float>(elapsed) / 45.f);
    const float fall = 1.f - std::exp(-static_cast<float>(elapsed) / 220.f);
    const auto approach = [&](float& value, uint8_t wanted) {
      if (first) value = wanted;
      else value += (wanted - value) * (wanted > value ? rise : fall);
      return static_cast<uint8_t>(std::lround(std::clamp(value, 0.f, 255.f)));
    };
    FrameStats out;
    for (int i = 0; i < kBandCount; ++i) out.bands[i] = approach(bands_[i], target.bands[i]);
    out.level = approach(level_, target.level);
    out.beat = target.beat;
    at_ = nowMs;
    return out;
  }
  void reset() { at_ = -1; }

 private:
  float bands_[kBandCount]{};
  float level_ = 0;
  int64_t at_ = -1;
};

}
