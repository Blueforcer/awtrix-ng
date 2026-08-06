#pragma once

#include <string>

#include "core/render/Canvas.h"
#include "core/render/Font.h"
#include "core/render/ScrollResolver.h"
#include "core/render/TextRenderer.h"

namespace awtrix {
namespace render {

// Baseline row for body text on the 8 px panel.
constexpr int kTextBaseline = 6;

// Draws the run with its pen at x. In Loop mode it also tiles copies either side so the wrap has
// no visible seam; advance is the run's own width, used to place those copies.
void drawScrollRun(Canvas& c, const GfxFont& font, float x, int baselineY, const std::string& run,
                   int advance, const text::TextPaint& paint, const ResolvedScroll* scroll);

}
}
