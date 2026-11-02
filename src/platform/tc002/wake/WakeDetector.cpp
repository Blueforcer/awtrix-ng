#include "platform/tc002/wake/WakeDetector.h"

namespace awtrix::tc002::wake {

bool WakeDetector::load(std::string_view bytes, std::string& error) {
  if (!stream_.load(bytes, error)) return false;
  forgetDetections();
  return true;
}

bool WakeDetector::configure(const WakeConfig& config, std::string& error) {
  if (!window_.configure(config.cutoff, config.window, error)) return false;
  modelCutoff_ = config.cutoff;
  window_.setCutoff(wakeCutoff(modelCutoff_, sensitivity_));
  cooloff_ = uint64_t{config.cooloffMs} * WakeFrontend::kSamplesPerMs;
  warmup_ = uint64_t{config.warmupMs} * WakeFrontend::kSamplesPerMs;
  forgetDetections();
  return true;
}

void WakeDetector::setSensitivity(WakeSensitivity level) {
  if (level == sensitivity_) return;
  sensitivity_ = level;
  window_.setCutoff(wakeCutoff(modelCutoff_, level));
}

void WakeDetector::reset() {
  stream_.reset();
  forgetDetections();
}

void WakeDetector::forgetDetections() {
  window_.reset();
  fired_ = false;
  last_ = 0;
}

std::optional<WakeEvent> WakeDetector::decide(uint8_t probability, uint64_t sample) {
  if (!window_.push(probability) || sample < warmup_ || (fired_ && sample - last_ <= cooloff_)) return std::nullopt;
  fired_ = true;
  last_ = sample;
  return WakeEvent{sample, window_.score()};
}

std::optional<WakeEvent> WakeDetector::feed(const int16_t* samples, std::size_t count) {
  std::optional<WakeEvent> heard;
  uint8_t probability = 0;
  while (stream_.next(samples, count, probability))
    if (const std::optional<WakeEvent> event = decide(probability, stream_.position())) heard = event;
  return heard;
}

}
