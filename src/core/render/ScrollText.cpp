#include "core/render/ScrollText.h"

#include <cmath>

namespace awtrix {
namespace render {

void drawScrollRun(Canvas& c, const GfxFont& font, float x, int baselineY, const std::string& run,
                   int advance, const text::TextPaint& paint, const ResolvedScroll* scroll,
                   bool smoothScroll) {
  // Snapping to whole pixels is what turns off drawRun's sub-pixel blending.
  if (!smoothScroll) x = std::floor(x);

  const bool loops =
      scroll && scroll->animates() && scroll->mode == ScrollMode::Loop && scroll->period > 0;
  if (!loops) {
    text::drawRun(c, font, x, baselineY, run, paint);
    return;
  }

  const int period = scroll->period;
  // Step back to the last copy that is still fully off the left edge, then tile rightwards until
  // the canvas is covered.
  float first = x;
  while (first + advance > 0) first -= period;
  for (float bx = first; bx < c.width(); bx += period)
    text::drawRun(c, font, bx, baselineY, run, paint);
}

}
}
