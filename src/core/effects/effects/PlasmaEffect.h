#pragma once

#include <cstdint>
#include <cmath>

#include "core/effects/IEffect.h"
#include "core/effects/PlasmaField.h"
#include "core/render/Color.h"

namespace awtrix {

class PlasmaEffect : public IEffect {
 public:
  const std::string& id() const override { return id_; }
  float rate() const override { return rate::kContinuous; }
  void render(Canvas& c, int64_t frame) override {
    const float t = frame * kPhasePerStep;
    const int w = c.width(), h = c.height();

    fx::Axes& a = fx::axes();
    // Panels wider than the axis tables fall back to evaluating the sines per pixel.
    const bool tabled = a.fits(w, h);
    if (tabled)
      fx::sampleAxes(a, w, h, [&](int x) { return std::sin(x * 0.3f + t); },
                     [](int y) { return std::sin(y * 0.3f); },
                     [&](int d) { return std::sin(d * 0.2f + t * 0.5f); });

    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const float v = tabled ? a.x[x] + a.y[y] + a.d[x + y]
                               : std::sin(x * 0.3f + t) + std::sin(y * 0.3f) +
                                     std::sin((x + y) * 0.2f + t * 0.5f);
        // Three summed sines span -3..3; fold that to 0..1 for the palette index.
        const float u = (v + 3.0f) / 6.0f;
        const uint8_t idx = static_cast<uint8_t>(u * 255.0f);
        c.setPixel(x, y, paletteColorOr(idx, [u] {
                     int hue = static_cast<int>(u * 360.0f) % 360;
                     if (hue < 0) hue += 360;
                     return color::fromHsv(hue, 100, 55);
                   }));
      }
    }
  }

 private:
  std::string id_ = "Plasma";
};

}
