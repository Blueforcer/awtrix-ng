#include "core/render/ScrollResolver.h"

namespace awtrix {
namespace render {

bool ResolvedScroll::operator==(const ResolvedScroll& o) const {
  return mode == o.mode && direction == o.direction && entry == o.entry &&
         whenFits == o.whenFits && speedPercent == o.speedPercent && gap == o.gap &&
         holdMs == o.holdMs &&
         layout.text.advance == o.layout.text.advance &&
         layout.text.inkLeft == o.layout.text.inkLeft &&
         layout.text.inkRight == o.layout.text.inkRight &&
         layout.availWidth == o.layout.availWidth &&
         layout.startX == o.layout.startX && layout.canvasWidth == o.layout.canvasWidth &&
         layout.textOffset == o.layout.textOffset;
}

ResolvedScroll resolve(const ScrollSpec& spec, const ScrollDefaults& defaults,
                       const ScrollLayout& layout) {
  ResolvedScroll r;
  r.layout = layout;
  r.mode = spec.hasMode ? spec.mode : defaults.mode;
  r.direction = spec.hasDirection ? spec.direction : defaults.direction;
  r.entry = spec.hasEntry ? spec.entry : defaults.entry;
  r.whenFits = spec.hasWhenFits ? spec.whenFits : defaults.whenFits;
  r.gap = spec.hasGap ? spec.gap : defaults.gap;
  r.holdMs = spec.hasHoldMs ? spec.holdMs : defaults.holdMs;
  r.speedPercent = static_cast<float>(spec.hasSpeed ? spec.speed : defaults.speed);

  r.deriveAnchors();
  return r;
}

void ResolvedScroll::deriveAnchors() {
  const float span = static_cast<float>(layout.text.advance + layout.textOffset);
  const float edge = static_cast<float>(layout.startX + layout.availWidth);
  const float atIcon = static_cast<float>(layout.startX);
  // Right-aligning uses the ink bounds, not the advance width, so trailing space in the string
  // does not leave a visible gap at the edge.
  const float atFarEdge =
      layout.text.hasInk()
          ? edge - 1.0f - static_cast<float>(layout.text.inkRight + layout.textOffset)
          : edge - span;

  const bool toLeft = direction == ScrollDirection::Left;
  xRest = toLeft ? atIcon : atFarEdge;
  xOff = toLeft ? edge : -span;
  xEnd = toLeft ? -span : edge;

  xNear = atIcon;
  xFar = atFarEdge;
  // Loop mode tiles the run every period pixels, so the gap is what separates the repeats.
  period = layout.text.advance + layout.textOffset + gap;
}

}
}
