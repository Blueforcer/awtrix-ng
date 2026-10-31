#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "platform/tc002/wake/ModelStream.h"

namespace awtrix::tc002::wake {

// probability_cutoff and sliding_window_size come from the model's custom.json.
struct WakeConfig {
  float cutoff = 0.99f;
  std::size_t window = 3;
  uint32_t cooloffMs = 1000;
  // Audio after a reset in which nothing fires and no peak counts: the frontend's noise estimate
  // starts at zero, so its first frames look loud. 0 decides like the trainer's benchmark.
  uint32_t warmupMs = 0;
};

// A detection: the stream position (samples since reset) at the end of the frames that fired, the
// window mean then (0..1) and the highest probability within the window (0..1).
struct WakeEvent {
  uint64_t sample = 0;
  float score = 0.0f;
  float peak = 0.0f;
};

// The decision of the trainer's streaming benchmark (wwtrainer eval/streaming.py): the mean of the
// last `window` probabilities, each raw / 255, fires at or above `cutoff` once the window is full and
// more than the cool-off after the previous detection. The trainer counts the cool-off in invokes of
// 10 ms; here it is audio time, the same for models of stride 1.
class WakeWindow {
 public:
  static constexpr std::size_t kMaxWindow = 64;

  // Set up with WakeConfig's defaults.
  WakeWindow();
  // False and a reason for a cutoff outside 0..1 or a window outside 1..64; the old setting stays.
  bool configure(const WakeConfig& config, std::string& error);
  // One model output, `sample` its stream position: true when it fires, with `event` filled.
  bool push(uint8_t probability, uint64_t sample, WakeEvent& event);
  void reset();

  // The current window mean, 0 until the window is full.
  float mean() const;
  // The highest window mean since the previous call.
  float takePeak();

 private:
  std::array<uint8_t, kMaxWindow> ring_{};
  std::size_t window_ = 1;
  std::size_t filled_ = 0;
  std::size_t next_ = 0;
  uint32_t sum_ = 0;
  uint32_t threshold_ = 256;
  uint32_t peak_ = 0;
  uint64_t cooloff_ = 16000;
  uint64_t warmup_ = 0;
  uint64_t last_ = 0;
  bool fired_ = false;
};

// The wake-word engine for one microphone stream: 16 kHz PCM in, features, the model every N frames
// with the features quantised exactly as the trainer fed them, detections out. Never allocates in
// feed(); not thread-safe.
class WakeDetector {
 public:
  WakeDetector() = default;

  // A .tflite file; it must have one uint8 output. False and a reason otherwise, keeping the model
  // there was. The stream starts over.
  bool load(std::string_view bytes, std::string& error);
  bool loaded() const { return stream_.loaded(); }
  // Keeps the stream; the window starts over. False for a cutoff outside 0..1 or a window outside
  // 1..64, keeping the old setting.
  bool configure(float cutoff, std::size_t window, uint32_t cooloffMs = 1000, uint32_t warmupMs = 0);

  // Any block size; nothing happens without a model. The latest detection within the block.
  std::optional<WakeEvent> feed(const int16_t* samples, std::size_t count);

  // The latest model output, raw / 255.
  float probability() const { return probability_ / 255.0f; }
  // The highest window mean since the previous call, for tuning the cutoff.
  float windowPeak() { return window_.takePeak(); }
  float windowMean() const { return window_.mean(); }
  // Samples fed since reset.
  uint64_t position() const { return stream_.position(); }

  // Back to a fresh stream: frontend, model state, window and position.
  void reset();

  // nullptr before load().
  const WakeModel* model() const { return stream_.model(); }

 private:
  ModelStream stream_;
  WakeWindow window_;
  uint8_t probability_ = 0;
};

}
