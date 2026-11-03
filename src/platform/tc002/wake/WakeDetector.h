#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "platform/tc002/wake/ModelStream.h"
#include "platform/tc002/wake/ProbabilityWindow.h"
#include "platform/tc002/wake/WakeSensitivity.h"

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

class WakeDetector {
 public:
  WakeDetector() = default;

  bool load(std::string_view bytes, std::string& error);
  bool load(std::shared_ptr<const posix::MappedFile> file, std::string& error);
  bool configure(const WakeConfig& config, std::string& error);
  void setSensitivity(WakeSensitivity level);

  std::optional<WakeEvent> feed(const int16_t* samples, std::size_t count);
  std::optional<WakeEvent> decide(uint8_t probability, uint64_t sample);
  void reset();

 private:
  void forgetDetections();

  ModelStream stream_;
  ProbabilityWindow window_;
  float modelCutoff_ = 0.0f;
  WakeSensitivity sensitivity_ = WakeSensitivity::Normal;
  uint64_t cooloff_ = 0;
  uint64_t warmup_ = 0;
  uint64_t last_ = 0;
  bool fired_ = false;
};

}
