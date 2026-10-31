#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "platform/tc002/wake/WakeFft.h"

namespace awtrix::tc002::wake {

// The microWakeWord features: TensorFlow Lite Micro's microfrontend (Hann window, int16 FFT, mel
// filterbank, noise reduction, PCAN gain control, log scale) with the settings of pymicro_features,
// which the trainer computes its features with. 16 kHz mono, 30 ms windows every 10 ms, 40 channels;
// every frame equals pymicro_features bit for bit. Never allocates.
class WakeFrontend {
 public:
  static constexpr std::size_t kChannels = 40;
  static constexpr std::size_t kStepSamples = 160;
  static constexpr std::size_t kWindowSamples = 480;
  using Frame = std::array<uint16_t, kChannels>;

  // Takes samples from the front of [samples, samples + count) until a frame is complete and
  // advances both. True with `frame` filled; false when the samples ran out first, which are kept for
  // the next call. Any block size works.
  bool next(const int16_t*& samples, std::size_t& count, Frame& frame);
  // Back to the state of a new frontend: no samples kept, noise estimates cleared.
  void reset();

 private:
  void compute(Frame& frame);

  std::array<int16_t, kWindowSamples> input_{};
  std::size_t inputUsed_ = 0;
  WakeFft fft_;
  WakeFft::Input fftInput_{};
  WakeFft::Spectrum spectrum_{};

  // As large as the original's buffer: padded channels read zero-weighted bins past the spectrum.
  std::array<uint32_t, 2 * (WakeFft::kHalf + 1)> energy_{};
  std::array<uint64_t, kChannels + 1> work_{};

  std::array<uint32_t, kChannels> signal_{};
  std::array<uint32_t, kChannels> estimate_{};
};

// A frame value the way the trainer stores it: value / 25.6 rounded half to even, at most 127.
inline uint8_t trainerFeature(uint16_t value) {
  const uint32_t scaled = uint32_t{value} * 5;
  uint32_t whole = scaled >> 7;
  const uint32_t rest = scaled & 127;
  if (rest > 64 || (rest == 64 && (whole & 1))) ++whole;
  return static_cast<uint8_t>(whole < 127 ? whole : 127);
}

}
