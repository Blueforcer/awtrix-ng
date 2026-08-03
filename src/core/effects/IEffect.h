#pragma once

#include <cstdint>

#include <string>

#include "core/effects/EffectRates.h"
#include "core/render/Canvas.h"
#include "core/render/ColorRamp.h"

namespace awtrix {

struct EffectSettings {
  float speed = 1.0f;
  bool hasSpeed = false;
  render::ColorRamp ramp;
};

class IEffect {
 public:
  virtual ~IEffect() = default;
  virtual const std::string& id() const = 0;
  virtual void render(Canvas& canvas, int64_t frame) = 0;

  // False for effects with hard-coded colours; the API leaves those out of the palette lists.
  virtual bool usesPalette() const { return true; }

  virtual float rate() const { return rate::kSteady; }

  virtual void setSettings(const EffectSettings& s) { settings_ = s; }
  const EffectSettings& settings() const { return settings_; }

  // The frame counter handed to render(): wall-clock ms scaled by the effect's own rate and the
  // user's speed setting, in units of kBaseStepMs.
  int64_t animationStep(int64_t nowMs) const {
    const float f = rate() * (settings_.hasSpeed ? settings_.speed : 1.0f);
    return static_cast<int64_t>(nowMs * static_cast<double>(f) / kBaseStepMs);
  }

 protected:
  uint32_t paletteColor(uint8_t index, uint32_t fallback) const {
    if (!settings_.ramp.valid()) return fallback;
    return settings_.ramp.atIndex(index);
  }

  template <typename F>
  uint32_t paletteColorOr(uint8_t index, F&& fallback) const {
    if (!settings_.ramp.valid()) return fallback();
    return settings_.ramp.atIndex(index);
  }
  bool hasPalette() const { return settings_.ramp.valid(); }

  EffectSettings settings_;
};

}
