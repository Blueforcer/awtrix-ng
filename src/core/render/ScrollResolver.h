#pragma once

#include "core/payload/ScrollSpec.h"
#include "core/render/TextRenderer.h"

namespace awtrix {
namespace render {

// Geometry of the text area in canvas pixels: startX is the left edge after any icon and
// availWidth what is left of the panel from there.
struct ScrollLayout {
  text::TextMetrics text;
  int availWidth = 0;
  int startX = 0;
  int canvasWidth = 32;
  int textOffset = 0;
};

struct ResolvedScroll {
  ScrollMode mode = ScrollMode::Wrap;
  ScrollDirection direction = ScrollDirection::Left;
  ScrollEntry entry = ScrollEntry::Inline;
  ScrollWhenFits whenFits = ScrollWhenFits::Static;
  float speedPercent = 100.f;
  int gap = 8;
  long holdMs = 1000;
  ScrollLayout layout;

  // Pen positions in canvas x, derived from the layout: xRest is where static text sits, xOff the
  // off-screen entry point, xEnd where a pass ends, xNear/xFar the two bounce endpoints.
  float xRest = 0;
  float xOff = 0;
  float xEnd = 0;
  float xNear = 0;
  float xFar = 0;
  int period = 0;

  void deriveAnchors();

  bool overflows() const { return layout.text.inkWidth() > layout.availWidth; }
  bool animates() const {
    return mode != ScrollMode::Static && (whenFits == ScrollWhenFits::Scroll || overflows());
  }

  bool operator==(const ResolvedScroll& o) const;
  bool operator!=(const ResolvedScroll& o) const { return !(*this == o); }
};

ResolvedScroll resolve(const ScrollSpec& spec, const ScrollDefaults& defaults,
                       const ScrollLayout& layout);

}
}
