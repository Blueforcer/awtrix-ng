#pragma once

#include <cstdint>

namespace awtrix {

inline constexpr uint16_t kAdcFullScale = 4095;

struct LightConfig {
  float factor = 1.0f;
  float gamma = 2.2f;
  bool onGround = false;
  uint8_t minBrightness = 10;
  uint8_t maxBrightness = 220;
};

float lightLevelFromRaw(uint16_t raw, const LightConfig& cfg);

uint8_t brightnessFromLightLevel(float levelPercent, const LightConfig& cfg);

class BrightnessSmoother {
 public:
  void setTimeConstant(long tauMs) { tauMs_ = tauMs > 0 ? tauMs : 0; }

  void reset(uint8_t value) {
    current_ = static_cast<float>(value);
    primed_ = true;
  }

  uint8_t apply(uint8_t target, long dtMs);

 private:
  float current_ = 0.0f;
  long tauMs_ = 0;
  bool primed_ = false;
};

}
