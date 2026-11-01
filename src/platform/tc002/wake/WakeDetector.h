#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "platform/tc002/wake/ModelStream.h"

namespace awtrix::tc002::wake {

struct WakeConfig {
  float cutoff = 0.99f;
  std::size_t window = 3;
  uint32_t cooloffMs = 1000;
  uint32_t warmupMs = 0;
};

struct WakeEvent {
  uint64_t sample = 0;
  float score = 0.0f;
};

class WakeWindow {
 public:
  static constexpr std::size_t kMaxWindow = 64;

  WakeWindow();
  bool configure(const WakeConfig& config, std::string& error);
  bool push(uint8_t probability, uint64_t sample, WakeEvent& event);
  void reset();

 private:
  float mean() const;

  std::array<uint8_t, kMaxWindow> ring_{};
  std::size_t window_ = 1;
  std::size_t filled_ = 0;
  std::size_t next_ = 0;
  uint32_t sum_ = 0;
  uint32_t threshold_ = 256;
  uint64_t cooloff_ = 0;
  uint64_t warmup_ = 0;
  uint64_t last_ = 0;
  bool fired_ = false;
};

class WakeDetector {
 public:
  WakeDetector() = default;

  bool load(std::string_view bytes, std::string& error);
  bool configure(const WakeConfig& config, std::string& error);

  std::optional<WakeEvent> feed(const int16_t* samples, std::size_t count);
  void reset();

 private:
  ModelStream stream_;
  WakeWindow window_;
};

}
