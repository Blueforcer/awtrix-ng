#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/render/Palette.h"

namespace awtrix {
namespace render {

struct ColorRamp {
  std::shared_ptr<const Palette> pal;
  bool blend = true;
  uint16_t spanPx = 0;
  float speed = 0.0f;

  bool valid() const { return static_cast<bool>(pal); }

  const Palette& palette() const { return pal ? *pal : defaultPalette(); }

  uint32_t atIndex(uint8_t index) const { return colorFromPalette(palette(), index, blend); }

  // t is 0..1 across the ramp. It maps onto index 0..240 so blending never wraps from the last
  // palette entry back to the first.
  uint32_t at(float t) const;

  // Scroll offset in pixels for an animated ramp: speed is wraps per second, spanPx the wrap
  // length (falling back to the width of the run being drawn).
  int originAt(int64_t nowMs, int runWidthPx) const;
};

Palette paletteFromStops(const uint32_t* stops, std::size_t n);

struct PaletteStop {
  uint32_t color;
  uint8_t pos;
};

Palette paletteFromPositionedStops(const PaletteStop* stops, std::size_t n);

}
}
