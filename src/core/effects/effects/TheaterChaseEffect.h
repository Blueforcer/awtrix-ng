#pragma once

#include <cstdint>
#include "core/effects/IEffect.h"

namespace awtrix {

class TheaterChaseEffect : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kSteady; }
  void render(Canvas& c, int64_t frame) override {
    const int off = static_cast<int>(frame % 3);
    for (int y = 0; y < c.height(); ++y)
      for (int x = 0; x < c.width(); ++x)
        c.setPixel(x, y, ((x + off) % 3 == 0)
                             ? paletteColor(static_cast<uint8_t>(x * 8 + frame), 0x202020u)
                             : 0x000000u);
  }

 private:
  std::string id_ = "TheaterChase";
};

}
