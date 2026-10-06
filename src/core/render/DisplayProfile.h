#pragma once

#include <cstddef>
#include <cstdint>

namespace awtrix {

// Geometry limits admitted by the platform. An empty profile admits nothing.
// These are memory limits, without a frame-rate guarantee.
struct DisplayLimits {
  int minWidth;
  int maxWidth;
  int minHeight;
  int maxHeight;
  std::size_t maxPixels;

  constexpr bool accepts(int width, int height) const {
    return width >= minWidth && width <= maxWidth && height >= minHeight &&
           height <= maxHeight && height > 0 &&
           static_cast<std::size_t>(width) <= maxPixels / static_cast<std::size_t>(height);
  }
};

constexpr DisplayLimits hostDisplayLimits() { return {8, 128, 8, 32, 4096}; }

// Logical pixels exposed to apps, scripts and the screen API. Electrical panel wiring belongs
// to the platform backend (MatrixLayout on ESP), never to this coordinate system.
struct DisplayProfile {
  int width = 32;
  int height = 8;
  bool configurable = true;
  DisplayLimits limits{};
  // Nominal single-wire data time only, excluding reset, rendering and driver overhead.
  uint32_t estimatedWireTimeUs = 0;
  int requestedWidth = 0;
  bool ready = true;

  constexpr bool valid() const { return width > 0 && height > 0; }
  constexpr std::size_t pixelCount() const {
    return valid() ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height) : 0;
  }
};

}
