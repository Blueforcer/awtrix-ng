#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "platform/tc002/wake/WakeFft.h"

namespace awtrix::tc002::wake {

class WakeFrontend {
 public:
  static constexpr std::size_t kChannels = 40;
  static constexpr uint32_t kSampleRate = 16000;
  static constexpr uint32_t kSamplesPerMs = kSampleRate / 1000;
  static constexpr std::size_t kStepMs = 10, kWindowMs = 30;
  static constexpr std::size_t kStepSamples = kStepMs * kSamplesPerMs;
  static constexpr std::size_t kWindowSamples = kWindowMs * kSamplesPerMs;
  using Frame = std::array<uint16_t, kChannels>;

  bool next(const int16_t*& samples, std::size_t& count, Frame& frame);
  void reset();

 private:
  void compute(Frame& frame);

  std::array<int16_t, kWindowSamples> input_{};
  std::size_t inputUsed_ = 0;
  WakeFft fft_;
  WakeFft::Input fftInput_{};
  WakeFft::Spectrum spectrum_{};

  std::array<uint32_t, 2 * (WakeFft::kHalf + 1)> energy_{};
  std::array<uint64_t, kChannels + 1> work_{};

  std::array<uint32_t, kChannels> signal_{};
  std::array<uint32_t, kChannels> estimate_{};
};

inline uint8_t trainerFeature(uint16_t value) {
  const uint32_t scaled = uint32_t{value} * 5;
  uint32_t whole = scaled >> 7;
  const uint32_t rest = scaled & 127;
  if (rest > 64 || (rest == 64 && (whole & 1))) ++whole;
  return static_cast<uint8_t>(whole < 127 ? whole : 127);
}

}
