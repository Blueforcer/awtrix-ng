#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace awtrix::speech {

// The vocoder's spectra as samples: a centred inverse STFT with a periodic Hann window, overlap-
// added and divided by the sum of the squared windows, then a DC blocker and int16. A chunk of T
// spectra gives (T - 1) * hop samples; the DC blocker runs on across the chunks of an utterance.
class SpeechWave {
 public:
  SpeechWave(std::size_t fftSize, std::size_t hop);

  // The most samples one push() writes.
  std::size_t maxSamples() const { return std::max(hop_, fftSize_ / 2); }

  void begin(std::size_t frames);
  // One spectrum: fftSize / 2 + 1 log-magnitudes, then as many phases. Writes the samples it
  // finished and returns how many.
  std::size_t push(const float* spectrum, int16_t* out);

 private:
  struct Complex {
    float re, im;
  };

  void inverse(float* samples);

  std::size_t fftSize_, hop_;
  std::vector<float> window_;
  std::vector<double> squares_;
  std::vector<Complex> twiddles_;
  std::vector<Complex> rotations_;
  std::vector<uint32_t> reversed_;
  std::vector<Complex> bins_;
  std::vector<Complex> work_;
  std::vector<float> frame_;
  // Overlap-add of the frames from the current one on, and of their squared windows.
  std::vector<double> sum_;
  std::vector<double> norm_;
  std::size_t frames_ = 0;
  std::size_t at_ = 0;
  double lastIn_ = 0.0;
  double lastOut_ = 0.0;
};

}
