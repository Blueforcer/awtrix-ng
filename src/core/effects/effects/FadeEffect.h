#pragma once

#include <cstdint>

#include "core/effects/EffectMath.h"
#include "core/effects/IEffect.h"

namespace awtrix {

class FadeEffect : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kContinuous; }
  void render(Canvas& c, int64_t frame) override {
    // The whole palette spread once from top to bottom, every row slowly cycling through it.
    for (int y = 0; y < c.height(); ++y) {
      const uint8_t idx = static_cast<uint8_t>(frame + y * 256 / c.height());
      c.fillRect(0, y, c.width(), 1, paletteColorOr(idx, [idx] { return fx::hueColor(idx, 60); }));
    }
  }

 private:
  std::string id_ = "Fade";
};

}
