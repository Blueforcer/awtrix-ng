#pragma once

#include <cstdint>
#include "core/effects/EffectMath.h"
#include "core/effects/IEffect.h"

namespace awtrix {

class TheaterChaseEffect : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kSteady; }
  void render(Canvas& c, int64_t frame) override {
    // Every third column lit, the lights marching over a palette spread once across the width.
    const int off = static_cast<int>(frame % 3);
    for (int x = 0; x < c.width(); ++x) {
      const uint8_t idx = static_cast<uint8_t>(x * 256 / c.width());
      c.fillRect(x, 0, 1, c.height(),
                 (x + off) % 3 == 0 ? paletteColorOr(idx, [idx] { return fx::hueColor(idx, 70); }) : 0u);
    }
  }

 private:
  std::string id_ = "TheaterChase";
};

}
