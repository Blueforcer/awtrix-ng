#pragma once

#include <cstdint>

#include "core/sensing/AutoBrightness.h"

namespace awtrix {

struct Settings;
struct RuntimeState;

// The one place that decides how bright the matrix is: moodlight, then the light sensor when the
// user asked for it and the board has one, then the manual setting. Platforms only feed readings.
class BrightnessPolicy {
 public:
  uint8_t resolve(const Settings& settings, const RuntimeState& runtime, const LightConfig& light,
                  int64_t nowMs);

 private:
  BrightnessSmoother smoother_;
  int64_t lastMs_ = 0;
  uint8_t autoLevel_ = 0;
  bool primed_ = false;
  bool autoActive_ = false;
};

}
