#pragma once

#include <cmath>
#include <cstdint>

namespace awtrix {
namespace sim {

// A deterministic tune for the host: a kick every half second, a bass note, hi-hats on the
// off-beats and an eight-second sweep, so a visualizer script has something to show.
class SimSong {
 public:
  static constexpr int kRateHz = 44100;

  void reset() { *this = SimSong{}; }

  void fill(int16_t* stereo, int frames) {
    constexpr float kPi = 3.14159265f;
    for (int i = 0; i < frames; ++i) {
      const float t = static_cast<float>(bar_) / kRateHz;
      float s = 0.9f * std::exp(-t * 18.f) * std::sin(2.f * kPi * (60.f + 80.f * std::exp(-t * 40.f)) * t);
      if (t < 0.35f) s += 0.25f * std::sin(bassPhase_);
      bassPhase_ = std::fmod(bassPhase_ + 2.f * kPi * 55.f / kRateHz, 2.f * kPi);
      const float tau = t - (t < 0.25f ? 0.125f : 0.375f);
      if (tau >= 0.f) {
        const float n = noise();
        s += 0.2f * std::exp(-tau * 60.f) * (n - lastNoise_);
        lastNoise_ = n;
      }
      const float f = 300.f * std::pow(2.f, 4.f * (0.5f - 0.5f * std::cos(2.f * kPi * sweepT_ / 8.f)));
      s += 0.15f * std::sin(sweepPhase_);
      sweepPhase_ = std::fmod(sweepPhase_ + 2.f * kPi * f / kRateHz, 2.f * kPi);
      sweepT_ += 1.f / kRateHz;
      if (sweepT_ >= 8.f) sweepT_ -= 8.f;
      s = s > 0.95f ? 0.95f : (s < -0.95f ? -0.95f : s);
      stereo[i * 2] = static_cast<int16_t>(s * 32767.f);
      stereo[i * 2 + 1] = static_cast<int16_t>(s * 0.8f * 32767.f);
      if (++bar_ >= kRateHz / 2) bar_ = 0;
    }
  }

 private:
  float noise() {
    lcg_ = lcg_ * 1664525u + 1013904223u;
    return static_cast<float>(lcg_ >> 8) / 8388608.f - 1.f;
  }

  int bar_ = 0;
  float bassPhase_ = 0.f;
  float sweepPhase_ = 0.f;
  float sweepT_ = 0.f;
  float lastNoise_ = 0.f;
  uint32_t lcg_ = 0x1234567u;
};

}
}
