#pragma once

#include <cstdint>

namespace awtrix {

// GFX metrics with height and advance limited to 15 pixels. bitmapOffset is a byte index into
// GfxFont::bitmap; xOffset/yOffset are relative to the pen sitting on the baseline.
struct FontGlyph {
  uint16_t bitmapOffset;
  uint8_t width;
  uint8_t height : 4;
  uint8_t xAdvance : 4;
  int8_t xOffset;
  int8_t yOffset;
};
static_assert(sizeof(FontGlyph) == 6, "glyph tables need six-byte records");

// Sparse block of codepoints outside the font's contiguous first..last span. index is 1-based so
// that 0 can mean "this font has no glyph for that codepoint".
struct FontRange {
  uint16_t first;
  uint16_t last;
  const uint16_t* index;
};



struct GfxFont {
  const uint8_t* bitmap;
  const FontGlyph* glyphs;
  uint16_t first;
  uint16_t last;
  uint8_t yAdvance;
  const FontRange* ranges = nullptr;
  uint8_t rangeCount = 0;
};

}
