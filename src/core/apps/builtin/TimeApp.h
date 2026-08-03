#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/apps/ClockText.h"
#include "core/apps/IApp.h"
#include "core/apps/builtin/ContentFrame.h"
#include "core/apps/builtin/WeekdayBar.h"
#include "core/render/TextRenderer.h"

namespace awtrix {

namespace detail {
// Masks of the black pixels punched out of a filled 6x7 block, not of the lit ones. Index 10 is
// ":" and 11 is a blank cell, which is why a leading space gets remapped to ";".
inline constexpr uint8_t kBigDigits[12][7] = {
    {132, 48, 48, 48, 48, 48, 132},
    {204, 140, 204, 204, 204, 204, 0},
    {132, 48, 240, 196, 156, 48, 0},
    {132, 48, 240, 196, 240, 48, 132},
    {228, 196, 132, 36, 0, 228, 192},
    {0, 60, 4, 240, 240, 48, 132},
    {196, 156, 60, 4, 48, 48, 132},
    {0, 48, 240, 228, 204, 204, 204},
    {132, 48, 48, 132, 48, 48, 132},
    {132, 48, 48, 128, 240, 228, 140},
    {252, 204, 204, 252, 204, 204, 252},
    {252, 252, 252, 252, 252, 252, 252},
};

inline constexpr uint32_t kBinHour = 0xFF0000u;
inline constexpr uint32_t kBinMinute = 0x00FF00u;
inline constexpr uint32_t kBinSecond = 0x0000FFu;
inline constexpr uint32_t kBinOff = 0xFFFFFFu;
}

class TimeApp : public IApp {
 public:
  const std::string& id() const override { return id_; }

  void render(Canvas& c, const RenderCtx& ctx) override {
    const Settings& s = *ctx.settings;
    // timeMode: 0 plain, 1-4 add the calendar box (2 and 4 move the weekday bar to the top),
    // 5 big clock, 6 binary.
    const int mode = s.timeMode;
    if (mode == 5) return renderBigClock(c, ctx);
    if (mode == 6) return renderBinaryClock(c, ctx);

    const bool box = mode >= 1 && mode <= 4;
    const bool wdTop = (mode == 2 || mode == 4);
    const uint32_t col = s.timeColor.valueOr(s.textColor);

    TimeTextOptions o = timeOptionsFrom(s);
    if (box) {
      o.showSeconds = false;
      o.showAmPm = false;
    }
    const std::vector<TextRun> runs = buildTimeRuns(o, ctx.hour, ctx.minute, ctx.second);
    const int runsW = runsWidth(*ctx.font, runs);
    const int inset = box ? kBoxWidth : 0;
    const ContentFrame f = contentFrame(c, inset + runsW);
    const int textX = f.x + inset;
    const int textArea = f.width - inset;

    if (box) {
      c.fillRect(f.x, 0, kBoxWidth, 8, s.calendarBodyColor);
      if (mode <= 2) {
        c.fillRect(f.x, 0, kBoxWidth, 2, s.calendarHeaderColor);
      } else {
        c.drawLine(f.x + 1, 0, f.x + 2, 0, 0x000000);
        c.drawLine(f.x + 6, 0, f.x + 7, 0, 0x000000);
      }
      const std::string day = std::to_string(ctx.mday);
      const int off = ctx.mday < 10 ? 3 : 1;
      text::drawText(c, *ctx.font, f.x + off, 7, day, s.calendarTextColor);
    }

    const float lvl = separatorLevel(s.timeSeparatorMode, ctx.second, ctx.nowMs);
    const int x = textX + (textArea - runsW) / 2;
    const bool barBottom = s.weekdayBar.show && !wdTop;
    const int baseline = (box && !barBottom) ? 7 : 6;
    drawRuns(c, *ctx.font, x < textX ? textX : x, baseline, runs, col, lvl);

    if (s.weekdayBar.show) {
      const int y = wdTop ? 0 : (c.height() - 1);
      if (box)
        drawWeekdayBar(c, s.weekdayBar, ctx.weekday, textX, textArea, 2, y);
      else
        drawWeekdayBar(c, s.weekdayBar, ctx.weekday, f.x, f.width, 3, y);
    }
  }

 private:
  static constexpr int kBoxWidth = 9;

  // drawText advances one blank column past each run; drop the last one so centring stays even.
  static int runsWidth(const GfxFont& font, const std::vector<TextRun>& runs) {
    int w = 0;
    for (const TextRun& r : runs) w += text::width(font, r.text);
    return w > 0 ? w - 1 : w;
  }

  static void drawRuns(Canvas& c, const GfxFont& font, int x, int y,
                       const std::vector<TextRun>& runs, uint32_t col, float sepLevel) {
    for (const TextRun& r : runs) {
      if (r.separator && sepLevel <= 0.0f) {
        x += text::width(font, r.text);
        continue;
      }
      const uint32_t rc = r.separator ? scaleColor(col, sepLevel) : col;
      x += text::drawText(c, font, x, y, r.text, rc);
    }
  }

  static void renderBigClock(Canvas& c, const RenderCtx& ctx) {
    const Settings& s = *ctx.settings;
    const uint32_t bg = s.timeColor.valueOr(s.textColor);
    const int x0 = contentFrame(c).x;
    c.fillRect(x0, 0, kContentFrameWidth, c.height(), bg);

    int h = ctx.hour;
    if (!s.time24h) {
      h %= 12;
      if (h == 0) h = 12;
    }
    char t[6];
    std::snprintf(t, sizeof(t), s.timeLeadingZero ? "%02d:%02d" : "%2d:%02d", h, ctx.minute);
    if (t[0] == ' ') t[0] = ';';

    const float lvl = separatorLevel(s.timeSeparatorMode, ctx.second, ctx.nowMs);
    for (int i = 0; i < 5; ++i) {
      // The colon cell is narrower than a digit, so it and everything after it shift left.
      const int x = x0 + i * 7 - (i > 2 ? 2 : 0) - (i == 2 ? 1 : 0);
      drawBigGlyph(c, x, t[i]);
      if (i == 2 && s.timeSeparatorMode != kSepSteady) {
        const uint32_t dot = scaleColor(bg, lvl);
        for (int row : {1, 2, 4, 5}) {
          c.setPixel(x + 2, row, dot);
          c.setPixel(x + 3, row, dot);
        }
      }
    }
    c.drawLine(x0, 7, x0 + kContentFrameWidth - 1, 7, 0x000000u);
    c.drawLine(x0 + 6, 0, x0 + 6, 6, 0x000000u);
    c.drawLine(x0 + 25, 0, x0 + 25, 6, 0x000000u);
  }

  static void drawBigGlyph(Canvas& c, int x, char ch) {
    const int idx = ch - '0';
    if (idx < 0 || idx > 11) return;
    for (int row = 0; row < 7; ++row) {
      const uint8_t bits = detail::kBigDigits[idx][row];
      for (int col = 0; col < 6; ++col)
        if (bits & (0x80u >> col)) c.setPixel(x + col, row, 0x000000u);
    }
  }

  static void renderBinaryClock(Canvas& c, const RenderCtx& ctx) {
    const int rows[3] = {0, 3, 6};
    const int values[3] = {ctx.hour, ctx.minute, ctx.second};
    const uint32_t colors[3] = {detail::kBinHour, detail::kBinMinute, detail::kBinSecond};
    const int x0 = contentFrame(c).x;
    for (int r = 0; r < 3; ++r)
      for (int i = 0; i < 6; ++i) {
        const bool on = (values[r] >> (5 - i)) & 1;
        c.fillRect(x0 + 5 + i * 4, rows[r], 2, 2, on ? colors[r] : detail::kBinOff);
      }
  }

  std::string id_ = "Time";
};

}
