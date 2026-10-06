#include "core/apps/SpecRenderer.h"

#include "core/render/DrawProgram.h"
#include "core/render/Gfx2d.h"
#include "core/render/ScrollText.h"
#include "core/render/TextRenderer.h"

namespace awtrix {
namespace render {

namespace {

bool upperFor(TextCase tc, bool globalUppercase) {
  return tc == TextCase::Upper || (tc == TextCase::Inherit && globalUppercase);
}

const ColorRamp* rampFor(const AppSpecExtras& x, bool wanted) {
  return wanted && x.palette.valid() ? &x.palette : nullptr;
}

int textColumn(const SpecRender& r) { return r.iconWidth > 0 ? r.iconWidth + r.iconGap : 0; }

// The progress bar starts right at the icon's edge and runs under the gap, as on AWTRIX 3.
void renderProgress(Canvas& c, const AppSpec& s, int x0) {
  const AppSpecExtras& x = s.extras();
  const ColorSource fill(x.progressColor, rampFor(x, x.progressUsesPalette));
  drawProgress(c, x.progress, fill, x.progressTrackColor, x0);
}

void renderDecorations(Canvas& c, const AppSpec& s, const GfxFont& font, uint32_t textColor,
                       const SpecRender& r) {
  drawProgram(c, font, r.drawBaseline - 1, s.extras().draw, textColor);
  renderProgress(c, s, r.iconWidth);
  const int column = textColumn(r);
  const AppSpecExtras& x = s.extras();
  const ColorSource chart(x.hasChartColor ? x.chartColor : textColor,
                          rampFor(x, x.chartUsesPalette));
  drawBars(c, x.barChart, chart, x.chartAutoscale, column);
  drawLineChart(c, x.lineChart, chart, x.chartAutoscale, column);
}


void renderText(Canvas& c, const AppSpec& s, const GfxFont& font, uint32_t color,
                const SpecRender& r) {
  if (s.text.empty()) return;
  const bool upper = upperFor(s.textCase, r.uppercase);
  const text::TextMetrics m = text::measure(font, s.text, upper);
  const int total = m.advance;

  const int column = textColumn(r);
  const int avail = c.width() - column;
  const bool animates = r.scroll && r.scroll->animates();
  float x;
  // Text that is not scrolling is aligned in the space right of the icon and then clamped so it
  // can never run into it. Scrolling text takes the x the scroller worked out.
  if (!animates) {
    int xi = s.textAlign == Align::Start ? column
                                         : aligned(s.textAlign, column, avail, m.inkWidth()) - m.inkLeft;
    if (xi + m.inkLeft < column) xi = column - m.inkLeft;
    x = static_cast<float>(xi);
  } else {
    x = r.textX;
  }
  x += static_cast<float>(s.textOffsetX);

  const AppSpecExtras& ex = s.extras();
  text::TextPaint paint;
  paint.flat = color;
  paint.upper = upper;
  paint.fadeMs = s.textFadeMs;
  paint.blinkMs = s.textBlinkMs;
  paint.nowMs = r.nowMs;
  paint.runs = s.fragments.data();
  paint.runCount = s.fragments.size();
  if (const ColorRamp* ramp = rampFor(ex, ex.textUsesPalette)) {
    paint.ramp = ramp;
    paint.rampOriginPx = ramp->originAt(r.nowMs, total);
  }

  // Scrolling text vanishes at the edge of the icon column rather than sliding up against the
  // icon, so the gap stays clear. Static text is left alone, textOffsetX included.
  if (animates) c.setClipX(r.textClipLeft, c.width() - 1);
  drawScrollRun(c, font, x, r.baseline, s.text, total, paint, r.scroll);
  if (animates) c.clearClipX();
}

}

text::TextMetrics textMetricsFor(const AppSpec& s, const GfxFont& font, bool globalUppercase) {
  return text::measure(font, s.text, upperFor(s.textCase, globalUppercase));
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
    renderDecorations(c, s, font, textColor, r);
    renderText(c, s, font, textColor, r);
  } else {
    renderText(c, s, font, textColor, r);
    renderDecorations(c, s, font, textColor, r);
  }
  // Dark red frame marks an app that outlived its lifetime and was kept rather than removed.
  if (s.lifeTimeEnd) c.drawRect(0, 0, c.width(), c.height(), 0x6e0700u);
}

}
}
