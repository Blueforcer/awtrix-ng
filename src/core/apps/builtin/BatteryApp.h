#pragma once

#include <cstdint>

#include "core/apps/IApp.h"
#include "core/apps/SensorFormat.h"
#include "core/apps/builtin/ContentFrame.h"
#include "core/render/Color.h"
#include "core/render/TextRenderer.h"

namespace awtrix {

class BatteryApp : public IApp {
 public:
  const std::string& id() const override { return id_; }
  void render(Canvas& c, const RenderCtx& ctx) override {
    const Settings& s = *ctx.settings;
    int pct = ctx.runtime->batteryPercent;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    const std::string str = formatBattery(pct);
    const int ink = text::measure(*ctx.font, str).inkWidth();
    const ContentFrame f = contentFrame(c, kIconWidth + ink);
    drawGauge(c, f.x, pct, ctx.runtime->lowBattery);
    text::drawCenteredIn(c, *ctx.font, str, 6, s.batteryColor.valueOr(s.textColor),
                         f.x + kIconWidth, f.width - kIconWidth);
  }

 private:
  struct FillRow {
    int x, y, w;
  };
  // Fill cells listed bottom-up; the last one is the narrow tip inside the shell.
  static constexpr FillRow kRows[] = {{2, 6, 3}, {2, 5, 3}, {2, 4, 3},
                                      {2, 3, 3}, {2, 2, 3}, {3, 1, 1}};
  static constexpr int kCells = 6;
  static constexpr int kIconWidth = 8;

  static uint32_t levelColor(int pct, bool low) {
    if (low || pct < 20) return 0xFF2000u;
    if (pct < 40) return 0xFFA000u;
    return 0x00E000u;
  }

  static uint32_t dim(uint32_t rgb, uint8_t s) {
    return color::pack(color::scale8(color::red(rgb), s), color::scale8(color::green(rgb), s),
                       color::scale8(color::blue(rgb), s));
  }

  static void drawGauge(Canvas& c, int x0, int pct, bool low) {
    const uint32_t col = levelColor(pct, low);
    const uint32_t shell = dim(col, 76);
    c.fillRect(x0 + 2, 0, 3, 1, shell);
    c.setPixel(x0 + 2, 1, shell);
    c.setPixel(x0 + 4, 1, shell);
    c.fillRect(x0 + 1, 1, 1, 7, shell);
    c.fillRect(x0 + 5, 1, 1, 7, shell);
    c.fillRect(x0 + 1, 7, 5, 1, shell);

    const float cells = static_cast<float>(pct) * kCells / 100.0f;
    const int full = static_cast<int>(cells);
    for (int i = 0; i < full && i < kCells; ++i)
      c.fillRect(x0 + kRows[i].x, kRows[i].y, kRows[i].w, 1, col);
    // The cell in progress is drawn dimmed, so the gauge moves between whole cells instead of
    // jumping.
    const float frac = cells - static_cast<float>(full);
    if (full < kCells && frac > 0.001f) {
      const uint32_t part = dim(col, static_cast<uint8_t>(128.0f + frac * 127.0f));
      c.fillRect(x0 + kRows[full].x, kRows[full].y, kRows[full].w, 1, part);
    }
  }

  std::string id_ = "Battery";
};

}
