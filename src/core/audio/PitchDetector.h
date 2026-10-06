#pragma once

#include <array>
#include <cstdint>

namespace awtrix {
namespace audio {

// The fundamental frequency of one mono window, by YIN (de Cheveigne and Kawahara, 2002):
// difference function, cumulative mean normalised difference, the first dip below an absolute
// threshold, parabolic interpolation. Stateless between calls, so the same window always answers
// the same number; the one scratch block is a member and nothing is allocated per call.
class PitchDetector {
 public:
  static constexpr float kMinHz = 70.f;
  static constexpr float kMaxHz = 1600.f;
  static constexpr float kThreshold = 0.15f;
  // AC RMS below this is silence; a DC offset alone never passes.
  static constexpr float kGateDbfs = -50.f;
  // The microphone's rate; the scratch block is sized for it.
  static constexpr int kMaxRateHz = 16000;

  // pcm is mono int16. 0 for silence, noise, chords, a pitch outside kMinHz..kMaxHz, a rate above
  // kMaxRateHz, or a window shorter than two of the longest periods.
  float detect(const int16_t* pcm, int samples, int sampleRateHz);

 private:
  // Lags 0..maxLag+1 at kMaxRateHz; +2 covers the rounding up and the interpolation neighbour.
  static constexpr int kMaxLag = static_cast<int>(kMaxRateHz / kMinHz) + 2;
  std::array<float, kMaxLag + 1> diff_{};
};

}
}
