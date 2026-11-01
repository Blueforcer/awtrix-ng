#include "platform/tc002/wake/WakeDetector.h"

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
    error = "the window must be within 1.." + std::to_string(kMaxWindow);
    return false;
  }
  window_ = config.window;
  const double scale = 255.0 * static_cast<double>(window_);
  const double cutoff = static_cast<double>(config.cutoff);
  double smallestFiringSum = std::ceil(cutoff * scale);
  while (smallestFiringSum > 0 && (smallestFiringSum - 1) / scale >= cutoff) --smallestFiringSum;
  while (smallestFiringSum / scale < cutoff) ++smallestFiringSum;
  threshold_ = static_cast<uint32_t>(smallestFiringSum);
  cooloff_ = uint64_t{config.cooloffMs} * WakeFrontend::kSamplesPerMs;
  warmup_ = uint64_t{config.warmupMs} * WakeFrontend::kSamplesPerMs;
  reset();
  return true;
}

void WakeWindow::reset() {
  filled_ = 0;
  next_ = 0;
  sum_ = 0;
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
  if (sum_ < threshold_ || (fired_ && sample - last_ <= cooloff_)) return false;
  fired_ = true;
  last_ = sample;
  event.sample = sample;
  event.score = mean();
  return true;
}

float WakeWindow::mean() const {
  return filled_ < window_ ? 0.0f : static_cast<float>(sum_ / (255.0 * static_cast<double>(window_)));
}

bool WakeDetector::load(std::string_view bytes, std::string& error) {
  if (!stream_.load(bytes, error)) return false;
  window_.reset();
  return true;
}

bool WakeDetector::configure(const WakeConfig& config, std::string& error) {
  return window_.configure(config, error);
}

void WakeDetector::reset() {
  stream_.reset();
  window_.reset();
}

std::optional<WakeEvent> WakeDetector::feed(const int16_t* samples, std::size_t count) {
  std::optional<WakeEvent> heard;
  uint8_t probability = 0;
  while (stream_.next(samples, count, probability)) {
    WakeEvent event;
    if (window_.push(probability, stream_.position(), event)) heard = event;
  }
  return heard;
}

}
