#include "platform/tc002/speech/SpeechWave.h"

#include <cmath>

namespace awtrix::speech {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDcPole = 0.9973;
constexpr float kMaxMagnitude = 100.0f;
constexpr float kMaxLogMagnitude = 60.0f;

int16_t pcm(double value) {
  auto v = static_cast<float>(value);
  if (!(v >= -1.0f && v <= 1.0f)) v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : 0.0f;
  const float scaled = v * 32767.0f;
  const float rounded = std::floor(std::fabs(scaled) + 0.5f);
  return static_cast<int16_t>(scaled < 0.0f ? -rounded : rounded);
}

}

SpeechWave::SpeechWave(std::size_t fftSize, std::size_t hop)
    : fftSize_(fftSize), hop_(hop), window_(fftSize), squares_(fftSize), twiddles_(fftSize / 4),
      rotations_(fftSize / 2), reversed_(fftSize / 2), bins_(fftSize / 2 + 1), work_(fftSize / 2),
      frame_(fftSize), sum_(fftSize), norm_(fftSize) {
  const std::size_t half = fftSize / 2;
  for (std::size_t i = 0; i < fftSize; ++i) {
    const double s = std::sin(kPi * static_cast<double>(i) / static_cast<double>(fftSize));
    window_[i] = static_cast<float>(s * s);
    squares_[i] = static_cast<double>(window_[i]) * window_[i];
  }
  for (std::size_t k = 0; k < twiddles_.size(); ++k) {
    const double angle = 2 * kPi * static_cast<double>(k) / static_cast<double>(half);
    twiddles_[k] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
  }
  for (std::size_t k = 0; k < half; ++k) {
    const double angle = 2 * kPi * static_cast<double>(k) / static_cast<double>(fftSize);
    rotations_[k] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
  }
  std::size_t bits = 0;
  while ((std::size_t{1} << bits) < half) ++bits;
  for (std::size_t k = 0; k < half; ++k) {
    uint32_t r = 0;
    for (std::size_t b = 0; b < bits; ++b) r |= static_cast<uint32_t>((k >> b) & 1u) << (bits - 1 - b);
    reversed_[k] = r;
  }
}

void SpeechWave::begin(std::size_t frames) {
  frames_ = frames;
  at_ = 0;
  std::fill(sum_.begin(), sum_.end(), 0.0);
  std::fill(norm_.begin(), norm_.end(), 0.0);
}

// The real inverse DFT of bins_[0..n/2], scaled by 1/n, through one complex inverse FFT of n/2
// points: even samples in the real parts, odd ones in the imaginary parts.
void SpeechWave::inverse(float* samples) {
  const std::size_t half = fftSize_ / 2;
  for (std::size_t k = 0; k < half; ++k) {
    const Complex a = bins_[k], b = {bins_[half - k].re, -bins_[half - k].im};
    const Complex even = {(a.re + b.re) * 0.5f, (a.im + b.im) * 0.5f};
    const Complex d = {(a.re - b.re) * 0.5f, (a.im - b.im) * 0.5f};
    const Complex r = rotations_[k];
    const Complex odd = {d.re * r.re - d.im * r.im, d.re * r.im + d.im * r.re};
    work_[reversed_[k]] = {even.re - odd.im, even.im + odd.re};
  }
  for (std::size_t length = 2; length <= half; length <<= 1) {
    const std::size_t step = half / length, span = length / 2;
    for (std::size_t i = 0; i < half; i += length) {
      for (std::size_t j = 0; j < span; ++j) {
        const Complex w = twiddles_[j * step];
        Complex& u = work_[i + j];
        Complex& v = work_[i + j + span];
        const Complex t = {v.re * w.re - v.im * w.im, v.re * w.im + v.im * w.re};
        v = {u.re - t.re, u.im - t.im};
        u = {u.re + t.re, u.im + t.im};
      }
    }
  }
  const float scale = 1.0f / static_cast<float>(half);
  for (std::size_t m = 0; m < half; ++m) {
    samples[2 * m] = work_[m].re * scale;
    samples[2 * m + 1] = work_[m].im * scale;
  }
}

std::size_t SpeechWave::push(const float* spectrum, int16_t* out) {
  const std::size_t half = fftSize_ / 2;
  const float* phases = spectrum + half + 1;
  bins_[0] = bins_[half] = {0.0f, 0.0f};
  for (std::size_t k = 1; k < half; ++k) {
    const float magnitude = std::min(kMaxMagnitude, std::exp(std::min(kMaxLogMagnitude, spectrum[k])));
    bins_[k] = {magnitude * std::cos(phases[k]), magnitude * std::sin(phases[k])};
  }
  inverse(frame_.data());
  for (std::size_t i = 0; i < fftSize_; ++i) {
    sum_[i] += static_cast<double>(frame_[i] * window_[i]);
    norm_[i] += squares_[i];
  }
  // Samples are counted from the first frame's centre; the buffers start at this frame.
  const std::size_t base = at_ * hop_;
  const std::size_t last = half + (frames_ - 1) * hop_;
  const std::size_t done = ++at_ == frames_ ? last : at_ * hop_;
  std::size_t written = 0;
  for (std::size_t i = std::max(base, half); i < std::min(done, last); ++i) {
    const std::size_t at = i - base;
    const double value = norm_[at] > 1e-11 ? sum_[at] / norm_[at] : 0.0;
    const double input = static_cast<float>(value);
    lastOut_ = input - lastIn_ + kDcPole * lastOut_;
    lastIn_ = input;
    out[written++] = pcm(lastOut_);
  }
  std::copy(sum_.begin() + static_cast<std::ptrdiff_t>(hop_), sum_.end(), sum_.begin());
  std::copy(norm_.begin() + static_cast<std::ptrdiff_t>(hop_), norm_.end(), norm_.begin());
  std::fill(sum_.end() - static_cast<std::ptrdiff_t>(hop_), sum_.end(), 0.0);
  std::fill(norm_.end() - static_cast<std::ptrdiff_t>(hop_), norm_.end(), 0.0);
  return written;
}

}
