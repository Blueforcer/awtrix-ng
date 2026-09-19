#pragma once

#include <cstdint>

#include "core/effects/IEffect.h"
#include "core/payload/AppSpec.h"
#include "core/render/Canvas.h"
#include "core/render/Font.h"
#include "core/render/ScrollResolver.h"

namespace awtrix {
namespace render {

struct SpecRender {
  uint32_t defaultTextColor = 0xFFFFFFu;
  // The icon's own width; text and charts start iconGap columns right of it.
  int iconWidth = 0;
  int iconGap = 0;
  // First column scrolling text may light; everything left of it belongs to the icon and its gap.
  int textClipLeft = 0;
  int64_t nowMs = 0;
  float textX = 0;
  const ResolvedScroll* scroll = nullptr;
  bool uppercase = false;
  IEffect* effect = nullptr;
  bool backgroundDrawn = false;
};

void renderSpec(Canvas& c, const AppSpec& s, const GfxFont& font, const SpecRender& r);

text::TextMetrics textMetricsFor(const AppSpec& s, const GfxFont& font, bool globalUppercase);

}
}
