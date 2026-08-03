#pragma once

#include <cstdint>

#include "core/effects/IEffect.h"
#include "core/effects/overlays/WeatherOverlays.h"

namespace awtrix {

class RainOverlay : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kSteady; }
  void render(Canvas& c, int64_t frame) override {
    detail::rainColumns(c, frame, {3, 1, 2, 0}, paletteColor(200, 0x0033AAu),
                        paletteColor(60, 0x001133u));
  }

 private:
  std::string id_ = "rain";
};

}
