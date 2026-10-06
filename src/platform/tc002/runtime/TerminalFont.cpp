#include "platform/tc002/runtime/TerminalFont.h"

namespace awtrix {
namespace render {
namespace terminal {

namespace {

// A two-column '1'.
constexpr uint8_t kNarrowOne[kGlyphH] = {0b001, 0b011, 0b001, 0b001, 0b001};

struct Shape {
  const uint8_t* rows = nullptr;
  int left = 0;
  int width = kGlyphW;
};

Shape proportional(char ch) {
  Shape s;
  s.rows = ch == '1' ? kNarrowOne : glyphRows(ch);
  if (!s.rows) return s;
  uint8_t ink = 0;
  for (int r = 0; r < kGlyphH; ++r) ink |= s.rows[r];
  if (!ink) return s;
  int left = 0, right = kGlyphW - 1;
  while (!(ink & (1u << (kGlyphW - 1 - left)))) ++left;
  while (!(ink & (1u << (kGlyphW - 1 - right)))) --right;
  s.left = left;
  s.width = right - left + 1;
  return s;
}

}

const uint8_t* glyphRows(char ch) { return tc002_glyph_rows(ch); }

void drawCell(Canvas& c, int x, int y, char ch, uint32_t rgb) {
  const uint8_t* rows = glyphRows(ch);
  if (!rows) return;
  for (int row = 0; row < kGlyphH; ++row)
    for (int col = 0; col < kGlyphW; ++col)
      if (rows[row] & (1u << (kGlyphW - 1 - col))) c.setPixel(x + col, y + row, rgb);
}

int textWidth(std::string_view s) {
  int w = 0;
  for (char ch : s) w += proportional(ch).width + 1;
  return w > 0 ? w - 1 : 0;
}

void drawText(Canvas& c, int x, int y, std::string_view s, uint32_t rgb) {
  for (char ch : s) {
    const Shape g = proportional(ch);
    if (g.rows)
      for (int r = 0; r < kGlyphH; ++r)
        for (int col = 0; col < g.width; ++col)
          if (g.rows[r] & (1u << (kGlyphW - 1 - g.left - col))) c.setPixel(x + col, y + r, rgb);
    x += g.width + 1;
  }
}

}
}
}
