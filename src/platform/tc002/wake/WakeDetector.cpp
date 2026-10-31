#include "platform/tc002/wake/WakeDetector.h"

#include <algorithm>
#include <cmath>

namespace awtrix::tc002::wake {

WakeWindow::WakeWindow() {
  std::string error;
  configure(WakeConfig{}, error);
}

bool WakeWindow::configure(const WakeConfig& config, std::string& error) {
  if (!(config.cutoff >= 0.0f && config.cutoff <= 1.0f)) {
    error = "the cutoff must be within 0..1";
    return false;
  }
  if (config.window < 1 || config.window > kMaxWindow) {
    error = "the window must be within 1..64";
    return false;
  }
  window_ = config.window;
  const double scale = 255.0 * static_cast<double>(window_);
  const double cutoff = static_cast<double>(config.cutoff);
  // The smallest sum whose mean reaches the cutoff.
  double sum = std::ceil(cutoff * scale);
  while (sum > 0 && (sum - 1) / scale >= cutoff) --sum;
  while (sum / scale < cutoff) ++sum;
  threshold_ = static_cast<uint32_t>(sum);
  cooloff_ = uint64_t{config.cooloffMs} * 16;
  warmup_ = uint64_t{config.warmupMs} * 16;
  reset();
  return true;
}

void WakeWindow::reset() {
  filled_ = 0;
  next_ = 0;
  sum_ = 0;
  peak_ = 0;
  fired_ = false;
  last_ = 0;
}

bool WakeWindow::push(uint8_t probability, uint64_t sample, WakeEvent& event) {
  if (filled_ == window_) sum_ -= ring_[next_];
  ring_[next_] = probability;
  sum_ += probability;
  next_ = (next_ + 1) % window_;
  if (filled_ < window_) ++filled_;
  if (filled_ < window_ || sample < warmup_) return false;
  peak_ = std::max(peak_, sum_);
  if (sum_ < threshold_ || (fired_ && sample - last_ <= cooloff_)) return false;
  fired_ = true;
  last_ = sample;
  event.sample = sample;
  event.score = mean();
  const auto end = ring_.begin() + static_cast<std::ptrdiff_t>(window_);
  event.peak = *std::max_element(ring_.begin(), end) / 255.0f;
  return true;
}

float WakeWindow::mean() const {
  return filled_ < window_ ? 0.0f : static_cast<float>(sum_ / (255.0 * static_cast<double>(window_)));
}

float WakeWindow::takePeak() {
  const float peak = static_cast<float>(peak_ / (255.0 * static_cast<double>(window_)));
  peak_ = 0;
  return peak;
}

bool WakeDetector::load(std::string_view bytes, std::string& error) {
  if (!stream_.load(bytes, error)) return false;
  window_.reset();
  probability_ = 0;
  return true;
}

bool WakeDetector::configure(float cutoff, std::size_t window, uint32_t cooloffMs, uint32_t warmupMs) {
  std::string error;
  return window_.configure({cutoff, window, cooloffMs, warmupMs}, error);
}

void WakeDetector::reset() {
  stream_.reset();
  window_.reset();
  probability_ = 0;
}

std::optional<WakeEvent> WakeDetector::feed(const int16_t* samples, std::size_t count) {
  std::optional<WakeEvent> heard;
  uint8_t probability = 0;
  while (stream_.next(samples, count, probability)) {
    probability_ = probability;
    WakeEvent event;
    if (window_.push(probability, stream_.position(), event)) heard = event;
  }
  return heard;
}

}
