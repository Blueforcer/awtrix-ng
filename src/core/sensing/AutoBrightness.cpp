#include "core/sensing/AutoBrightness.h"

#include <algorithm>
#include <cmath>

namespace awtrix {

float lightLevelFromRaw(uint16_t raw, const LightConfig& cfg) {
  float counts = static_cast<float>(std::min<uint16_t>(raw, kAdcFullScale));
  // Boards that wire the photoresistor to ground read backwards: dark gives a high count.
  if (cfg.onGround) counts = kAdcFullScale - counts;
  const float factor = cfg.factor > 0.0f ? cfg.factor : 1.0f;
  const float level = counts / kAdcFullScale * factor * 100.0f;
  return std::min(std::max(level, 0.0f), 100.0f);
}

uint8_t brightnessFromLightLevel(float levelPercent, const LightConfig& cfg) {
  float n = std::min(std::max(levelPercent, 0.0f), 100.0f) / 100.0f;
  if (cfg.gamma > 0.0f && cfg.gamma != 1.0f) n = std::pow(n, cfg.gamma);
  const float lo = static_cast<float>(std::min(cfg.minBrightness, cfg.maxBrightness));
  const float hi = static_cast<float>(std::max(cfg.minBrightness, cfg.maxBrightness));
  const float bri = lo + n * (hi - lo);
  return static_cast<uint8_t>(std::lround(std::min(std::max(bri, 0.0f), 255.0f)));
}

uint8_t BrightnessSmoother::apply(uint8_t target, long dtMs) {
  if (!primed_ || tauMs_ <= 0 || dtMs <= 0) {
    reset(target);
    return target;
  }
  // Exponential smoothing derived from the real elapsed time, so an irregular tick rate does not
  // change how fast the display follows the light. tauMs_ is the time to cover ~63% of a step.
  const float alpha = 1.0f - std::exp(-static_cast<float>(dtMs) / static_cast<float>(tauMs_));
  current_ += (static_cast<float>(target) - current_) * alpha;
  const float rounded = std::lround(current_);
  return static_cast<uint8_t>(std::min(std::max(rounded, 0.0f), 255.0f));
}

}
