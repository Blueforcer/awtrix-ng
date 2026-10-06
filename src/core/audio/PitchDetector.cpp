#include "core/audio/PitchDetector.h"

#include <cmath>

namespace awtrix {
namespace audio {

namespace {

// Lag 1 always normalises to 1; the search starts at lag 2.
constexpr int kFirstLag = 2;

}

float PitchDetector::detect(const int16_t* pcm, int samples, int sampleRateHz) {
  if (!pcm || sampleRateHz <= 0 || sampleRateHz > kMaxRateHz) return 0.f;
  const int maxLag = static_cast<int>(std::ceil(sampleRateHz / kMinHz));
  const int lastLag = maxLag + 1;
  const int width = samples - lastLag;
  if (width < maxLag) return 0.f;

  int64_t sum = 0, squares = 0;
  for (int i = 0; i < samples; ++i) {
    sum += pcm[i];
    squares += static_cast<int64_t>(pcm[i]) * pcm[i];
  }
  const double mean = static_cast<double>(sum) / samples;
  const double variance = static_cast<double>(squares) / samples - mean * mean;
  const double gate = 32768.0 * std::pow(10.0, kGateDbfs / 20.0);
  if (variance < gate * gate) return 0.f;

  diff_[0] = 0.f;
  double running = 0.0;
  double bestNorm = 0.0;
  int best = -1;
  for (int lag = 1; lag <= lastLag; ++lag) {
    int64_t acc = 0;
    const int16_t* shifted = pcm + lag;
    for (int j = 0; j < width; ++j) {
      const int32_t d = static_cast<int32_t>(pcm[j]) - shifted[j];
      acc += static_cast<int64_t>(d) * d;
    }
    diff_[lag] = static_cast<float>(acc);
    // The first non-improving lag is also the right interpolation neighbour. At
    // the search boundary compute that neighbour without admitting a lower tone.
    if (lag > maxLag) break;
    running += diff_[lag];
    const double norm = running > 0.0 ? diff_[lag] * lag / running : 1.0;
    if (best < 0) {
      if (lag >= kFirstLag && norm < kThreshold) { best = lag; bestNorm = norm; }
    } else if (norm < bestNorm) {
      best = lag;
      bestNorm = norm;
    } else {
      break;
    }
  }
  if (best < 0) return 0.f;

  // Interpolates on the raw difference.
  const double a = diff_[best - 1], b = diff_[best], c = diff_[best + 1];
  const double curve = a - 2.0 * b + c;
  double shift = curve > 0.0 ? 0.5 * (a - c) / curve : 0.0;
  if (shift < -1.0) shift = -1.0;
  if (shift > 1.0) shift = 1.0;
  const double hz = sampleRateHz / (best + shift);
  if (hz < kMinHz || hz > kMaxHz) return 0.f;
  return static_cast<float>(hz);
}

}
}
