#pragma once

#include <cstdint>
#include <string>

namespace awtrix {
namespace render {

struct Palette {
  uint32_t entries[16];
};

// index 0..255 spread across the 16 entries. With blend the low nibble interpolates toward the
// next entry, wrapping from entry 15 back to entry 0.
uint32_t colorFromPalette(const Palette& pal, uint8_t index, bool blend);

const Palette& namedPalette(const std::string& name);

const Palette* findStockPalette(const std::string& name);

const Palette& defaultPalette();

}
}
