#pragma once

#include <cstdint>
#include <cmath>

#include "core/effects/IEffect.h"
#include "core/render/Color.h"

namespace awtrix {

class FadeEffect : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kContinuous; }
  void render(Canvas& c, int64_t frame) override {
    // Whole-canvas pulse: it walks the palette if there is one, otherwise it breathes dark blue.
    const float phase = std::sin(frame * kPhasePerStep) * 0.5f + 0.5f;
    const int b = static_cast<int>(phase * 90.0f);
    c.clear(paletteColor(static_cast<uint8_t>(phase * 255.0f), color::fromRgb(0, 0, b)));
  }

 private:
  std::string id_ = "Fade";
};

}
