#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/render/Canvas.h"
#include "core/render/ColorRamp.h"
#include "core/render/Font.h"

namespace awtrix {
namespace text {

int charAdvance(const GfxFont& font, uint32_t cp);

int width(const GfxFont& font, const std::string& s);

// advance is how far the pen moves for the whole string; inkLeft/inkRight are the first and last
// columns that actually light up, relative to the pen start. inkRight < inkLeft means no ink.
struct TextMetrics {
  int advance = 0;
  int inkLeft = 0;
  int inkRight = -1;

  bool hasInk() const { return inkRight >= inkLeft; }
  int inkWidth() const { return hasInk() ? inkRight - inkLeft + 1 : 0; }
};

TextMetrics measure(const GfxFont& font, const std::string& s);

// Throughout this header y is the baseline row, not the top of the glyph, and the return value is
// the x advance that was consumed.
int drawChar(Canvas& canvas, const GfxFont& font, int x, int y, uint32_t cp, uint32_t color);

int drawGlyph(Canvas& canvas, const GfxFont& font, int x, int y, const FontGlyph* g,
              uint32_t color);

int drawText(Canvas& canvas, const GfxFont& font, int x, int y, const std::string& s, uint32_t color);

// Colour source for a run, in falling priority: ramp (sampled per pixel column), then glyphColors
// (one entry per glyph), then flat.
struct TextPaint {
  uint32_t flat = 0xFFFFFFu;
  const render::ColorRamp* ramp = nullptr;
  const uint32_t* glyphColors = nullptr;
  std::size_t glyphCount = 0;
  int rampOriginPx = 0;

  uint32_t glyphColorAt(int i) const {
    if (!glyphColors || i < 0 || static_cast<std::size_t>(i) >= glyphCount) return flat;
    return glyphColors[i];
  }
};

// x is fractional: the sub-pixel part splits every lit pixel across two columns and the result is
// alpha-blended over the canvas, which is what makes slow scrolling look smooth.
int drawRun(Canvas& canvas, const GfxFont& font, float x, int y, const std::string& s,
            const TextPaint& paint);

int drawTextF(Canvas& canvas, const GfxFont& font, float x, int y, const std::string& s,
              uint32_t color);

int drawCenteredIn(Canvas& canvas, const GfxFont& font, const std::string& s, int baselineY,
                   uint32_t color, int x0, int areaWidth);

int drawCentered(Canvas& canvas, const GfxFont& font, const std::string& s, int baselineY,
                 uint32_t color, int x0 = 0);

}
}
