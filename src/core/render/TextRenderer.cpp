#include "core/render/TextRenderer.h"

#include <algorithm>

#include "core/render/Color.h"
#include "core/render/TextEncoding.h"

namespace awtrix {
namespace text {

int charAdvance(const GfxFont& font, uint32_t cp) {
  const FontGlyph* g = glyphFor(font, cp);
  return g ? g->xAdvance : 0;
}

int width(const GfxFont& font, const std::string& s) {
  GlyphIter it(font, s);
  const FontGlyph* g = nullptr;
  int w = 0;
  while (it.next(g))
    if (g) w += g->xAdvance;
  return w;
}

namespace {

bool glyphIsBlank(const GfxFont& font, const FontGlyph& g) {
  const int bytes = (g.width * g.height + 7) / 8;
  const uint8_t* bits = font.bitmap + g.bitmapOffset;
  for (int i = 0; i < bytes; ++i)
    if (bits[i]) return false;
  return true;
}

void glyphInkColumns(const GfxFont& font, const FontGlyph& g, int& left, int& right) {
  const uint8_t* bits = font.bitmap + g.bitmapOffset;
  uint16_t bit = 0;
  uint8_t cur = 0;
  left = g.width;
  right = -1;
  for (int yy = 0; yy < g.height; ++yy) {
    for (int xx = 0; xx < g.width; ++xx) {
      if ((bit & 7) == 0) cur = bits[bit >> 3];
      const bool on = cur & 0x80;
      ++bit;
      cur <<= 1;
      if (!on) continue;
      if (xx < left) left = xx;
      if (xx > right) right = xx;
    }
  }
}

}

// Ink bounds track the first and last non-blank glyph, so leading and trailing spaces do not count
// toward centring or the scroll extents.
TextMetrics measure(const GfxFont& font, const std::string& s) {
  TextMetrics m;
  const FontGlyph* firstInked = nullptr;
  const FontGlyph* lastInked = nullptr;
  int firstAt = 0;
  int lastAt = 0;

  GlyphIter it(font, s);
  const FontGlyph* g = nullptr;
  while (it.next(g)) {
    if (!g) continue;
    if (!glyphIsBlank(font, *g)) {
      if (!firstInked) {
        firstInked = g;
        firstAt = m.advance;
      }
      lastInked = g;
      lastAt = m.advance;
    }
    m.advance += g->xAdvance;
  }

  if (!firstInked) return m;

  int left = 0, right = 0;
  glyphInkColumns(font, *firstInked, left, right);
  m.inkLeft = firstAt + firstInked->xOffset + left;
  glyphInkColumns(font, *lastInked, left, right);
  m.inkRight = lastAt + lastInked->xOffset + right;
  return m;
}

// Glyph bitmaps are 1 bit per pixel, most significant bit first, packed continuously with no
// padding between rows -- hence the running bit counter instead of a per-row index.
int drawGlyph(Canvas& canvas, const GfxFont& font, int x, int y, const FontGlyph* g,
              uint32_t color) {
  if (!g) return 0;
  const uint8_t* bits = font.bitmap + g->bitmapOffset;
  uint16_t bit = 0;
  uint8_t cur = 0;
  for (int yy = 0; yy < g->height; ++yy) {
    for (int xx = 0; xx < g->width; ++xx) {
      if ((bit & 7) == 0) cur = bits[bit >> 3];
      ++bit;
      if (cur & 0x80) canvas.setPixel(x + g->xOffset + xx, y + g->yOffset + yy, color);
      cur <<= 1;
    }
  }
  return g->xAdvance;
}

int drawChar(Canvas& canvas, const GfxFont& font, int x, int y, uint32_t cp, uint32_t color) {
  return drawGlyph(canvas, font, x, y, glyphFor(font, cp), color);
}

int drawText(Canvas& canvas, const GfxFont& font, int x, int y, const std::string& s, uint32_t color) {
  GlyphIter it(font, s);
  const FontGlyph* g = nullptr;
  int advance = 0;
  while (it.next(g)) advance += drawGlyph(canvas, font, x + advance, y, g, color);
  return advance;
}

namespace {

constexpr int kColCacheWidth = 32;

// Two ways to lay a ramp over a run: wrapping tiles it every span pixels and can drift over time,
// otherwise it is stretched exactly once across the string's ink.
struct RampSampler {
  const render::ColorRamp* ramp = nullptr;
  bool wrap = false;
  int span = 1;
  int origin = 0;

  bool active() const { return ramp != nullptr; }

  uint32_t at(int col) const {
    if (wrap) {
      int p = (col + origin) % span;
      if (p < 0) p += span;
      return ramp->atIndex(static_cast<uint8_t>((p * 256) / span));
    }
    int idx = (col * 240) / span;
    if (idx < 0) idx = 0;
    if (idx > 240) idx = 240;
    return ramp->atIndex(static_cast<uint8_t>(idx));
  }
};

RampSampler makeSampler(const TextPaint& paint, const GfxFont& font, const std::string& s) {
  RampSampler out;
  if (!paint.ramp || !paint.ramp->valid()) return out;
  out.ramp = paint.ramp;
  out.origin = paint.rampOriginPx;
  out.wrap = paint.ramp->spanPx > 0 || paint.ramp->speed != 0.0f;
  if (out.wrap) {
    out.span = paint.ramp->spanPx > 0 ? paint.ramp->spanPx : std::max(1, width(font, s));
    return out;
  }
  const TextMetrics m = measure(font, s);
  out.span = std::max(1, m.inkRight - m.inkLeft);
  out.origin -= m.inkLeft;
  return out;
}

}

int drawRun(Canvas& canvas, const GfxFont& font, int x, int y, const std::string& s,
            const TextPaint& paint) {
  if (canvas.width() <= 0 || canvas.height() <= 0) return width(font, s);

  const RampSampler sampler = makeSampler(paint, font, s);

  int advance = 0;
  int gi = 0;
  GlyphIter it(font, s);
  const FontGlyph* g = nullptr;
  while (it.next(g)) {
    if (g) {
      // The ramp only varies by column, so sample it once per glyph column instead of per lit
      // pixel. Wider glyphs than the cache fall back to sampling inline.
      uint32_t colCache[kColCacheWidth];
      const bool cached = sampler.active() && g->width <= kColCacheWidth;
      if (cached)
        for (int xx = 0; xx < g->width; ++xx)
          colCache[xx] = sampler.at(advance + g->xOffset + xx);

      uint32_t col = sampler.active() ? 0u : paint.glyphColorAt(gi);

      const uint8_t* bits = font.bitmap + g->bitmapOffset;
      uint16_t bit = 0;
      uint8_t cur = 0;
      for (int yy = 0; yy < g->height; ++yy) {
        const int cy = y + g->yOffset + yy;
        for (int xx = 0; xx < g->width; ++xx) {
          if ((bit & 7) == 0) cur = bits[bit >> 3];
          const bool on = cur & 0x80;
          ++bit;
          cur <<= 1;
          if (!on) continue;
          if (sampler.active())
            col = cached ? colCache[xx] : sampler.at(advance + g->xOffset + xx);
          canvas.setPixel(x + advance + g->xOffset + xx, cy, col);
        }
      }
      advance += g->xAdvance;
    }
    ++gi;
  }
  return advance;
}

// Centres the ink rather than the advance box, so side bearings and trailing spaces don't pull the
// text off centre. Never starts left of x0, even when the string is too wide.
int drawCenteredIn(Canvas& canvas, const GfxFont& font, const std::string& s, int baselineY,
                   uint32_t color, int x0, int areaWidth) {
  const TextMetrics m = measure(font, s);
  int x = x0 + (areaWidth - m.inkWidth()) / 2 - m.inkLeft;
  if (x + m.inkLeft < x0) x = x0 - m.inkLeft;
  drawText(canvas, font, x, baselineY, s, color);
  return x;
}

int drawCentered(Canvas& canvas, const GfxFont& font, const std::string& s, int baselineY,
                 uint32_t color, int x0) {
  return drawCenteredIn(canvas, font, s, baselineY, color, x0, canvas.width() - x0);
}

}
}
