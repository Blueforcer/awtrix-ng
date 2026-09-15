#pragma once

#include <cstdint>

namespace awtrix {

enum class PanelColorOrder : uint8_t { Rgb = 0, Rbg, Grb, Gbr, Brg, Bgr };

inline constexpr const char* kPanelColorOrderNames[] = {"rgb", "rbg", "grb",
                                                        "gbr", "brg", "bgr"};
inline constexpr int kPanelColorOrderCount = 6;

namespace render {

struct DriverColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// MatrixRenderer keeps FastLED's NEOPIXEL controller, whose wire order is GRB. Rearranging the
// logical channels in its CRGB buffer produces every physical three-channel order without
// instantiating a separate templated controller for every order and supported data pin.
inline DriverColor colorForGrbDriver(uint32_t color, PanelColorOrder order) {
  const uint8_t r = static_cast<uint8_t>((color >> 16) & 0xFFu);
  const uint8_t g = static_cast<uint8_t>((color >> 8) & 0xFFu);
  const uint8_t b = static_cast<uint8_t>(color & 0xFFu);
  switch (order) {
    case PanelColorOrder::Rgb: return {g, r, b};
    case PanelColorOrder::Rbg: return {b, r, g};
    case PanelColorOrder::Gbr: return {b, g, r};
    case PanelColorOrder::Brg: return {r, b, g};
    case PanelColorOrder::Bgr: return {g, b, r};
    case PanelColorOrder::Grb:
    default: return {r, g, b};
  }
}

}
}
