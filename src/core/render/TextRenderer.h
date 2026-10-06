#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/render/Canvas.h"
#include "core/render/ColorRamp.h"
#include "core/render/Font.h"

namespace awtrix {
namespace text {

int charAdvance(const GfxFont& font, uint32_t cp);

// upper measures and draws every codepoint as its capital, without copying the string.
int width(const GfxFont& font, std::string_view s, bool upper = false);

// advance is how far the pen moves for the whole string; inkLeft/inkRight are the first and last
// columns that actually light up, relative to the pen start. inkRight < inkLeft means no ink.
struct TextMetrics {
  int advance = 0;
  int inkLeft = 0;
  int inkRight = -1;
  int inkTop = 0;
  int inkBottom = -1;

  bool hasInk() const { return inkRight >= inkLeft; }
  int inkWidth() const { return hasInk() ? inkRight - inkLeft + 1 : 0; }
  int inkHeight() const { return hasInk() ? inkBottom - inkTop + 1 : 0; }
};

// Horizontal ink bounds and advance. Row bounds remain empty.
TextMetrics measure(const GfxFont& font, std::string_view s, bool upper = false);
// Also measures ink rows for vertical alignment.
TextMetrics measureInk(const GfxFont& font, std::string_view s, bool upper = false);

// Throughout this header y is the baseline row, not the top of the glyph, and the return value is
// the x advance that was consumed.
int drawChar(Canvas& canvas, const GfxFont& font, int x, int y, uint32_t cp, uint32_t color);

int drawGlyph(Canvas& canvas, const GfxFont& font, int x, int y, const FontGlyph* g,
              uint32_t color);

// Large type from one glyph: from the pen at x every glyph column becomes kx columns, and output row
// j below top shows the glyph row rows[j] rows under rowTop (counted from the baseline, like
// yOffset). The map may repeat rows or leave some out.
void drawGlyphRows(Canvas& canvas, const GfxFont& font, int x, int top, const FontGlyph* g, int kx,
                   int rowTop, const uint8_t* rows, int nRows, uint32_t color);

int drawText(Canvas& canvas, const GfxFont& font, int x, int y, std::string_view s, uint32_t color);

// A stretch of a string drawn in one colour; bytes is its length. kFlatColor stands for the
// paint's flat colour.
struct TextRun {
  uint32_t bytes = 0;
  uint32_t color = 0;
};
inline constexpr uint32_t kFlatColor = 0xFF000000u;

// fadeMs is one full sine cycle of brightness; blinkMs is a square wave that lights the second
// half of each period. Fade wins when both are set.
uint32_t pulse(uint32_t color, int fadeMs, int blinkMs, int64_t nowMs);

// Colour source for a run, in falling priority: ramp (sampled per pixel column), then runs, then
// flat. fadeMs and blinkMs pulse the flat and run colours, never the ramp.
struct TextPaint {
  uint32_t flat = 0xFFFFFFu;
  const render::ColorRamp* ramp = nullptr;
  const TextRun* runs = nullptr;
  std::size_t runCount = 0;
  int rampOriginPx = 0;
  bool upper = false;
  int fadeMs = 0;
  int blinkMs = 0;
  int64_t nowMs = 0;
};

int drawRun(Canvas& canvas, const GfxFont& font, int x, int y, std::string_view s,
            const TextPaint& paint);

int drawCenteredIn(Canvas& canvas, const GfxFont& font, std::string_view s, int baselineY,
                   uint32_t color, int x0, int areaWidth);

int drawCentered(Canvas& canvas, const GfxFont& font, std::string_view s, int baselineY,
                 uint32_t color, int x0 = 0);

}
}
