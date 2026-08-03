#include "core/apps/SpecRenderer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/StrCase.h"
#include "core/render/Color.h"
#include "core/render/Gfx2d.h"
#include "core/render/ScrollText.h"
#include "core/render/TextEncoding.h"
#include "core/render/TextRenderer.h"

namespace awtrix {
namespace render {

namespace {

constexpr int kBaseline = kTextBaseline;
constexpr int kIconTextGap = 8;

std::string maybeUpper(const std::string& in, TextCase tc, bool globalUppercase) {
  const bool up = (tc == TextCase::Upper) || (tc == TextCase::Inherit && globalUppercase);
  return up ? text::toUpperUtf8(in) : in;
}

// textFadeMs is one full sine cycle of brightness; textBlinkMs is a square wave that lights the
// second half of each period. Fade wins when both are set.
uint32_t textEffectColor(uint32_t color, const AppSpec& s, int64_t nowMs) {
  if (s.textFadeMs > 0) {
    const float phase =
        (std::sin(2.0f * 3.14159265f * nowMs / static_cast<float>(s.textFadeMs)) + 1.0f) * 0.5f;
    return color::pack(static_cast<uint8_t>(color::red(color) * phase),
                       static_cast<uint8_t>(color::green(color) * phase),
                       static_cast<uint8_t>(color::blue(color) * phase));
  }
  if (s.textBlinkMs > 0) return (nowMs % s.textBlinkMs > s.textBlinkMs / 2) ? color : 0x000000u;
  return color;
}

void applyDrawOps(Canvas& c, const AppSpec& s, const GfxFont& font, uint32_t textColor) {
  for (const DrawOp& op : s.extras().draw) {
    const uint32_t opColor = op.inheritColor ? textColor : op.color;
    switch (op.kind) {
      case DrawKind::Pixel: c.setPixel(op.x, op.y, opColor); break;
      case DrawKind::Pixels:
        for (std::size_t i = 0; i + 1 < op.points.size(); i += 2)
          c.setPixel(op.points[i], op.points[i + 1], opColor);
        break;
      case DrawKind::Line: c.drawLine(op.x, op.y, op.x2, op.y2, opColor); break;
      case DrawKind::Rect: c.drawRect(op.x, op.y, op.w, op.h, opColor); break;
      case DrawKind::FillRect: c.fillRect(op.x, op.y, op.w, op.h, opColor); break;
      case DrawKind::Circle: c.drawCircle(op.x, op.y, op.r, opColor); break;
      case DrawKind::FillCircle: c.fillCircle(op.x, op.y, op.r, opColor); break;
      // Draw ops give y as the top row of the text, the renderer wants a baseline.
      case DrawKind::Text: text::drawText(c, font, op.x, op.y + 5, op.text, opColor); break;
      case DrawKind::Bitmap: {
        std::size_t i = 0;
        for (int yy = 0; yy < op.h; ++yy)
          for (int xx = 0; xx < op.w; ++xx, ++i)
            if (i < op.bitmap.size()) c.setPixel(op.x + xx, op.y + yy, op.bitmap[i]);
        break;
      }
    }
  }
}

const ColorRamp* rampFor(const AppSpecExtras& x, bool wanted) {
  return wanted && x.palette.valid() ? &x.palette : nullptr;
}

void renderProgress(Canvas& c, const AppSpec& s, int iconWidth) {
  const int x0 = iconWidth > 0 ? kIconTextGap : 0;
  const AppSpecExtras& x = s.extras();
  const ColorSource fill(x.progressColor, rampFor(x, x.progressUsesPalette));
  drawProgress(c, x.progress, fill, x.progressTrackColor, x0);
}

void renderDecorations(Canvas& c, const AppSpec& s, const GfxFont& font, uint32_t textColor,
                       int iconWidth) {
  applyDrawOps(c, s, font, textColor);
  renderProgress(c, s, iconWidth);
  const AppSpecExtras& x = s.extras();
  const ColorSource chart(x.hasChartColor ? x.chartColor : textColor,
                          rampFor(x, x.chartUsesPalette));
  drawBars(c, x.barChart, chart, x.chartAutoscale, iconWidth);
  drawLineChart(c, x.lineChart, chart, x.chartAutoscale, iconWidth);
}


void renderText(Canvas& c, const AppSpec& s, const GfxFont& font, uint32_t color,
                const SpecRender& r) {
  const bool hasFragments = !s.fragments.empty();
  if (!hasFragments && s.text.empty()) return;

  const std::string textStr = maybeUpper(s.text, s.textCase, r.uppercase);
  std::vector<std::string> fragTexts;
  std::string fragRun;
  std::vector<uint32_t> fragColors;
  if (hasFragments) {
    fragTexts.reserve(s.fragments.size());
    for (const auto& f : s.fragments)
      fragTexts.push_back(maybeUpper(f.text, s.textCase, r.uppercase));
    for (std::size_t i = 0; i < s.fragments.size(); ++i) {
      fragRun += fragTexts[i];
      const uint32_t col = textEffectColor(s.fragments[i].color, s, r.nowMs);
      fragColors.insert(fragColors.end(), text::glyphCount(font, fragTexts[i]), col);
    }
  }

  const text::TextMetrics m = text::measure(font, hasFragments ? fragRun : textStr);
  const int total = m.advance;

  const int avail = c.width() - r.iconWidth;
  float x;
  // Text that is not scrolling is centred in the space left of the icon and then clamped so it
  // can never run into it. Scrolling text takes the x the scroller worked out.
  if (!r.scroll || !r.scroll->animates()) {
    int xi = s.textCenter ? (r.iconWidth + (avail - m.inkWidth()) / 2 - m.inkLeft) : r.iconWidth;
    if (xi + m.inkLeft < r.iconWidth) xi = r.iconWidth - m.inkLeft;
    x = static_cast<float>(xi);
  } else {
    x = r.textX;
  }
  x += static_cast<float>(s.textOffsetX);

  const AppSpecExtras& ex = s.extras();
  text::TextPaint paint;
  paint.flat = textEffectColor(color, s, r.nowMs);
  if (const ColorRamp* ramp = rampFor(ex, ex.textUsesPalette)) {
    paint.ramp = ramp;
    paint.rampOriginPx = ramp->originAt(r.nowMs, total);
  } else if (hasFragments) {
    paint.glyphColors = fragColors.data();
    paint.glyphCount = fragColors.size();
  }

  drawScrollRun(c, font, x, kBaseline, hasFragments ? fragRun : textStr, total, paint, r.scroll,
                r.smoothScroll);
}

}

text::TextMetrics textMetricsFor(const AppSpec& s, const GfxFont& font, bool globalUppercase) {
  if (!s.fragments.empty()) {
    std::string run;
    for (const auto& f : s.fragments) run += maybeUpper(f.text, s.textCase, globalUppercase);
    return text::measure(font, run);
  }
  return text::measure(font, maybeUpper(s.text, s.textCase, globalUppercase));
}

void renderSpec(Canvas& c, const AppSpec& s, const GfxFont& font, const SpecRender& r) {
  // The caller may have painted the background already; otherwise an effect or the flat
  // background color fills it.
  if (r.backgroundDrawn) {
  } else if (r.effect) {
    r.effect->render(c, r.effect->animationStep(r.nowMs));
  } else {
    c.clear(s.hasBackgroundColor ? s.backgroundColor : 0x000000u);
  }
  const uint32_t textColor = s.hasTextColor ? s.textColor : r.defaultTextColor;
  if (s.textInFront) {
    renderDecorations(c, s, font, textColor, r.iconWidth);
    renderText(c, s, font, textColor, r);
  } else {
    renderText(c, s, font, textColor, r);
    renderDecorations(c, s, font, textColor, r.iconWidth);
  }
  // Dark red frame marks an app that outlived its lifetime and was kept rather than removed.
  if (s.lifeTimeEnd) c.drawRect(0, 0, c.width(), c.height(), 0x6e0700u);
}

}
}
