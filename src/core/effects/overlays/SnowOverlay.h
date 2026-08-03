#pragma once

#include <cstdint>

#include "core/effects/EffectNoise.h"
#include "core/effects/IEffect.h"

namespace awtrix {

class SnowOverlay : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kDrift; }
  void render(Canvas& c, int64_t frame) override {
    // Only every other column can hold a flake, and sway nudges it one pixel sideways as it falls.
    const int span = c.height() + 4;
    for (int x = 0; x < c.width(); x += 2) {
      const uint32_t lane = noise::hash2(static_cast<uint32_t>(x), 0x534E4F57u);
      const int64_t pos = frame + static_cast<int>(lane % static_cast<uint32_t>(span));
      const uint32_t roll = noise::hash2(lane, static_cast<uint32_t>(pos / span));
      if (roll % 3u == 0) continue;
      const int y = static_cast<int>(pos % span) - 2;
      if (y < 0 || y >= c.height()) continue;
      const int sway = static_cast<int>((frame / 3 + (lane >> 4)) % 3) - 1;
      const int px = (x + sway + c.width()) % c.width();
      const bool bright = (roll >> 8) & 1u;
      c.setPixel(px, y, bright ? paletteColor(230, 0xCCCCCCu) : paletteColor(140, 0x777777u));
    }
  }

 private:
  std::string id_ = "snow";
};

}
