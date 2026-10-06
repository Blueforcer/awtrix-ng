#include "core/render/TextRenderer.h"

#include <algorithm>
#include <cmath>

#include "core/render/Color.h"
#include "core/render/TextEncoding.h"

namespace awtrix {
namespace text {

int charAdvance(const GfxFont& font, uint32_t cp) {
  const FontGlyph* g = glyphFor(font, cp);
  return g ? g->xAdvance : 0;
}

int width(const GfxFont& font, std::string_view s, bool upper) {
  GlyphIter it(font, s, upper);
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

template <bool Rows>
void glyphInk(const GfxFont& font, const FontGlyph& g, int& left, int& right,
              int& top, int& bottom) {
  const uint8_t* bits = font.bitmap + g.bitmapOffset;
  uint16_t bit = 0;
  uint8_t cur = 0;
  left = g.width;
  right = -1;
  if constexpr (Rows) {
    top = g.height;
    bottom = -1;
  }
  for (int yy = 0; yy < g.height; ++yy) {
    for (int xx = 0; xx < g.width; ++xx) {
      if ((bit & 7) == 0) cur = bits[bit >> 3];
      const bool on = cur & 0x80;
      ++bit;
      cur <<= 1;
      if (!on) continue;
      if (xx < left) left = xx;
      if (xx > right) right = xx;
      if constexpr (Rows) {
        if (yy < top) top = yy;
        bottom = yy;
      }
    }
  }
}

void glyphInkColumns(const GfxFont& font, const FontGlyph& g, int& left, int& right) {
  int unused = 0;
  glyphInk<false>(font, g, left, right, unused, unused);
}

}

// Ink bounds track the first and last non-blank glyph, so leading and trailing spaces do not count
// toward centring or the scroll extents.
TextMetrics measure(const GfxFont& font, std::string_view s, bool upper) {
  TextMetrics m;
  const FontGlyph* firstInked = nullptr;
  const FontGlyph* lastInked = nullptr;
  int firstAt = 0;
  int lastAt = 0;

  GlyphIter it(font, s, upper);
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

TextMetrics measureInk(const GfxFont& font, std::string_view s, bool upper) {
  TextMetrics m;
  bool haveInk = false;
  GlyphIter it(font, s, upper);
  const FontGlyph* g = nullptr;
  while (it.next(g)) {
    if (!g) continue;
    int left, right, top, bottom;
    glyphInk<true>(font, *g, left, right, top, bottom);
    if (right >= left) {
      if (!haveInk) {
        haveInk = true;
        m.inkLeft = m.advance + g->xOffset + left;
        m.inkTop = g->yOffset + top;
        m.inkBottom = g->yOffset + bottom;
      } else {
        m.inkTop = std::min(m.inkTop, g->yOffset + top);
        m.inkBottom = std::max(m.inkBottom, g->yOffset + bottom);
      }
      m.inkRight = m.advance + g->xOffset + right;
    }
    m.advance += g->xAdvance;
  }
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

void drawGlyphRows(Canvas& canvas, const GfxFont& font, int x, int top, const FontGlyph* g, int kx,
                   int rowTop, const uint8_t* rows, int nRows, uint32_t color) {
  if (!g) return;
  const uint8_t* bits = font.bitmap + g->bitmapOffset;
  uint16_t bit = 0;
  uint8_t cur = 0;
  for (int yy = 0; yy < g->height; ++yy) {
    const int row = g->yOffset + yy - rowTop;
    for (int xx = 0; xx < g->width; ++xx) {
      if ((bit & 7) == 0) cur = bits[bit >> 3];
      ++bit;
      const bool on = cur & 0x80;
      cur <<= 1;
      if (!on) continue;
      for (int j = 0; j < nRows; ++j)
        if (rows[j] == row) canvas.fillRect(x + (g->xOffset + xx) * kx, top + j, kx, 1, color);
    }
  }
}

int drawChar(Canvas& canvas, const GfxFont& font, int x, int y, uint32_t cp, uint32_t color) {
  return drawGlyph(canvas, font, x, y, glyphFor(font, cp), color);
}

int drawText(Canvas& canvas, const GfxFont& font, int x, int y, std::string_view s, uint32_t color) {
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

RampSampler makeSampler(const TextPaint& paint, const GfxFont& font, std::string_view s) {
  RampSampler out;
  if (!paint.ramp || !paint.ramp->valid()) return out;
  out.ramp = paint.ramp;
  out.origin = paint.rampOriginPx;
  out.wrap = paint.ramp->spanPx > 0 || paint.ramp->speed != 0.0f;
  if (out.wrap) {
    out.span = paint.ramp->spanPx > 0 ? paint.ramp->spanPx : std::max(1, width(font, s, paint.upper));
    return out;
  }
  const TextMetrics m = measure(font, s, paint.upper);
  out.span = std::max(1, m.inkRight - m.inkLeft);
  out.origin -= m.inkLeft;
  return out;
}

}

int drawRun(Canvas& canvas, const GfxFont& font, int x, int y, std::string_view s,
            const TextPaint& paint) {
  if (canvas.width() <= 0 || canvas.height() <= 0) return width(font, s, paint.upper);

  const RampSampler sampler = makeSampler(paint, font, s);
  const uint32_t flat = pulse(paint.flat, paint.fadeMs, paint.blinkMs, paint.nowMs);
  const auto runColor = [&](std::size_t i) {
    const uint32_t c = paint.runs[i].color;
    return c == kFlatColor ? flat : pulse(c, paint.fadeMs, paint.blinkMs, paint.nowMs);
  };
  const bool byRun = !sampler.active() && paint.runCount > 0;
  std::size_t run = 0;
  std::size_t runEnd = byRun ? paint.runs[0].bytes : 0;
  uint32_t col = byRun ? runColor(0) : flat;

  int advance = 0;
  GlyphIter it(font, s, paint.upper);
  const FontGlyph* g = nullptr;
  for (std::size_t at = it.offset(); it.next(g); at = it.offset()) {
    if (byRun && at >= runEnd && run + 1 < paint.runCount) {
      do runEnd += paint.runs[++run].bytes;
      while (at >= runEnd && run + 1 < paint.runCount);
      col = runColor(run);
    }
    if (g) {
      // The ramp only varies by column, so sample it once per glyph column instead of per lit
      // pixel. Wider glyphs than the cache fall back to sampling inline.
      uint32_t colCache[kColCacheWidth];
      const bool cached = sampler.active() && g->width <= kColCacheWidth;
      if (cached)
        for (int xx = 0; xx < g->width; ++xx)
          colCache[xx] = sampler.at(advance + g->xOffset + xx);

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
  }
  return advance;
}

uint32_t pulse(uint32_t color, int fadeMs, int blinkMs, int64_t nowMs) {
  if (fadeMs > 0) {
    const float phase =
        (std::sin(2.0f * 3.14159265f * nowMs / static_cast<float>(fadeMs)) + 1.0f) * 0.5f;
    return color::pack(static_cast<uint8_t>(color::red(color) * phase),
                       static_cast<uint8_t>(color::green(color) * phase),
                       static_cast<uint8_t>(color::blue(color) * phase));
  }
  if (blinkMs > 0) return (nowMs % blinkMs > blinkMs / 2) ? color : 0x000000u;
  return color;
}

// Centres the ink rather than the advance box, so side bearings and trailing spaces don't pull the
// text off centre. Never starts left of x0, even when the string is too wide.
int drawCenteredIn(Canvas& canvas, const GfxFont& font, std::string_view s, int baselineY,
                   uint32_t color, int x0, int areaWidth) {
  const TextMetrics m = measure(font, s);
  int x = x0 + (areaWidth - m.inkWidth()) / 2 - m.inkLeft;
  if (x + m.inkLeft < x0) x = x0 - m.inkLeft;
  drawText(canvas, font, x, baselineY, s, color);
  return x;
}

int drawCentered(Canvas& canvas, const GfxFont& font, std::string_view s, int baselineY,
                 uint32_t color, int x0) {
  return drawCenteredIn(canvas, font, s, baselineY, color, x0, canvas.width() - x0);
}

}
}
