#include "core/audio/Limiter.h"

#include <algorithm>
#include <cmath>

#include "core/audio/GainRamp.h"

namespace awtrix {
namespace audio {

Limiter::Limiter(uint32_t rate, std::size_t blockFrames)
    : perFrame_(1.0 - std::exp(-1000.0 / (static_cast<double>(kReleaseMs) * rate))),
      blockRelease_(1.0 - std::pow(1.0 - perFrame_, static_cast<double>(blockFrames))),
      blockFrames_(blockFrames) {}

void Limiter::apply(const int32_t* sum, int16_t* out, std::size_t frames) {
  int32_t peak = 0;
  std::size_t firstOver = frames;
  for (std::size_t i = 0; i < frames; ++i) {
    const int32_t magnitude = sum[i] < 0 ? -sum[i] : sum[i];
    peak = std::max(peak, magnitude);
    if (magnitude > kThreshold && firstOver == frames) firstOver = i;
  }
  if (gain_ == kUnity && peak <= kThreshold) {
    for (std::size_t i = 0; i < frames; ++i) out[i] = static_cast<int16_t>(sum[i]);
    return;
  }
  const int32_t need = peak > kThreshold
                           ? static_cast<int32_t>((static_cast<int64_t>(kThreshold) << 15) / peak)
                           : kUnity;
  const int32_t from = gain_;
  std::size_t reach = frames;
  if (need < from) {
    gain_ = need;
    reach = firstOver;
  } else {
    const double release = frames == blockFrames_ ? blockRelease_
        : 1.0 - std::pow(1.0 - perFrame_, static_cast<double>(frames));
    gain_ = from + static_cast<int32_t>(std::ceil((need - from) * release));
    if (need == kUnity && kUnity - gain_ < 16) gain_ = kUnity;
  }
  GainRamp transition(from, gain_, reach);
  for (std::size_t i = 0; i < frames; ++i) {
    const int32_t gain = i >= reach ? gain_ : transition.next();
    out[i] = static_cast<int16_t>(scaled(sum[i], gain));
  }
}

}
}
