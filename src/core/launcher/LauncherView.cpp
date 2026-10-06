#include "core/launcher/LauncherView.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/render/Canvas.h"
#include "core/launcher/MenuStyle.h"
#include "core/render/Color.h"
#include "core/render/Motion.h"
#include "core/render/TextRenderer.h"

namespace awtrix::launcher {

namespace {

using style::sprite;

constexpr int kRow = 8;
constexpr int kBaseline = 6;
constexpr int kGlyphTop = 1;
constexpr int kGlyphSpace = 7;
// Narrower panels give the whole row to the label.
constexpr int kGlyphMinWidth = 48;
using style::kDim;
constexpr float kScrollPxPerSec = 22.0f;
constexpr int kScrollGap = 12;

constexpr uint32_t kBlack = 0x000000u;
constexpr uint32_t kWhite = 0xFFFFFFu;
using style::kAmber;
using style::kCyan;
constexpr uint32_t kGreen = 0x40E060u;
constexpr uint32_t kRed = 0xFF4030u;
using style::kTrack;

constexpr const char* kPlay[] = {"#...", "##..", "###.", "##..", "#..."};
constexpr const char* kSpeaker[] = {"..#.#", "###..", "###.#", "###..", "..#.#"};
constexpr const char* kNote[] = {"..##", "..#.", "..#.", "###.", "##.."};
constexpr const char* kStop[] = {"....", "####", "####", "####", "####"};

// Three bars bouncing out of phase: the station that plays.
void equalizer(Canvas& canvas, int x, int y, uint32_t rgb, int64_t nowMs) {
  for (int bar = 0; bar < 3; ++bar) {
    const float wave = std::sin(static_cast<float>(nowMs) / 140.0f + static_cast<float>(bar) * 2.1f);
    const int height = 1 + static_cast<int>(std::lround((wave + 1.0f) * 2.0f));
    canvas.fillRect(x + bar * 2, y + 5 - height, 1, height, rgb);
  }
}

void glyph(Canvas& canvas, const Entry& entry, int x, int y, uint32_t rgb, int64_t nowMs) {
  switch (entry.kind) {
    case Entry::Kind::Scripts:
    case Entry::Kind::Script: sprite(canvas, x, y, kPlay, rgb); return;
    case Entry::Kind::Stop: sprite(canvas, x, y, kStop, rgb); return;
    case Entry::Kind::Radio:
    case Entry::Kind::Station:
      if (entry.playing) equalizer(canvas, x, y, rgb, nowMs);
      else if (entry.kind == Entry::Kind::Radio) sprite(canvas, x, y, kSpeaker, rgb);
      else sprite(canvas, x, y, kNote, rgb);
      return;
  }
}

uint32_t glyphColor(const Entry& entry) {
  switch (entry.kind) {
    case Entry::Kind::Scripts:
    case Entry::Kind::Script: return kAmber;
    case Entry::Kind::Stop: return kRed;
    case Entry::Kind::Radio:
    case Entry::Kind::Station: return entry.playing ? kGreen : kCyan;
  }
  return kWhite;
}

uint32_t labelColor(const Entry& entry) {
  if (entry.playing) return kGreen;
  if (entry.kind == Entry::Kind::Stop) return kRed;
  return kWhite;
}

using color::scale;

float settle(const Launcher& menu, int64_t nowMs) {
  return motion::easeOut(motion::clamp01(motion::progress(nowMs, menu.movedAtMs(), LauncherView::kMoveMs)));
}

// The selected entry brightens as the cursor arrives and the one it left fades back.
float lightOf(const Launcher& menu, int index, int64_t nowMs) {
  const float t = settle(menu, nowMs);
  return style::selectionLight(index, menu.selected(), menu.previousSelected(), t, !menu.pageChanged());
}

}

bool LauncherView::draw(Canvas& canvas, const Launcher& menu, const GfxFont& font, int64_t nowMs) {
  float reveal = 1.0f;
  if (menu.isOpen()) {
    reveal = motion::easeOut(motion::clamp01(motion::progress(nowMs, menu.openedAtMs(), kOpenMs)));
  } else {
    if (menu.closedAtMs() < 0 || nowMs - menu.closedAtMs() >= kOpenMs) return false;
    reveal = 1.0f - motion::easeIn(motion::clamp01(motion::progress(nowMs, menu.closedAtMs(), kOpenMs)));
  }

  // Opens from the middle outwards; outside the opening the app's frame stays.
  const int half = static_cast<int>(std::lround(reveal * static_cast<float>(canvas.width()) / 2.0f));
  const int left = canvas.width() / 2 - half;
  const ClipScope opening(canvas, left, 0, half * 2, canvas.height());
  canvas.fillRect(left, 0, half * 2, canvas.height(), kBlack);

  const int rows = std::max(1, canvas.height() / kRow);
  const int top = (canvas.height() - rows * kRow) / 2;
  if (!menu.notice().empty())
    text::drawCentered(canvas, font, menu.notice(), top + (rows - 1) * kRow / 2 + kBaseline, kWhite);
  else if (rows == 1)
    drawSingle(canvas, menu, font, top, nowMs);
  else
    drawRows(canvas, menu, font, top, rows, nowMs);
  return true;
}

void LauncherView::drawSingle(Canvas& canvas, const Launcher& menu, const GfxFont& font, int top,
                              int64_t nowMs) {
  const auto& entries = menu.entries();
  if (entries.empty()) return;
  const int width = canvas.width();
  const Entry& current = entries[menu.selected()];
  const float t = settle(menu, nowMs);
  {
    const ClipScope row(canvas, 0, top, width, kRow);
    if (t >= 1.0f) {
      drawEntry(canvas, current, font, 0, top, width, 1.0f, true, menu.movedAtMs(), nowMs);
    } else if (menu.pageChanged()) {
      const int dy = static_cast<int>(std::lround((1.0f - t) * kRow));
      drawEntry(canvas, current, font, 0, top + dy, width, 1.0f, false, 0, nowMs);
    } else {
      const int shift = static_cast<int>(std::lround(t * static_cast<float>(width)));
      const int dir = menu.moveDirection();
      const int previous = std::min(menu.previousSelected(), static_cast<int>(entries.size()) - 1);
      drawEntry(canvas, entries[previous], font, -dir * shift, top, width, 1.0f, false, 0, nowMs);
      drawEntry(canvas, current, font, dir * (width - shift), top, width, 1.0f, false, 0, nowMs);
    }
  }
  drawSlider(canvas, menu, top + kRow - 1);
}

// With more entries than rows the window follows the selection.
void LauncherView::drawRows(Canvas& canvas, const Launcher& menu, const GfxFont& font, int top,
                            int rows, int64_t nowMs) {
  const auto& entries = menu.entries();
  const int count = static_cast<int>(entries.size());
  const int first = std::max(0, std::min(menu.selected() - (rows - 1) / 2, count - rows));
  for (int i = 0; i < rows && first + i < count; ++i) {
    const int index = first + i;
    const int y = top + i * kRow;
    const ClipScope row(canvas, 0, y, canvas.width(), kRow);
    drawEntry(canvas, entries[index], font, 0, y, canvas.width(), lightOf(menu, index, nowMs),
              index == menu.selected(), menu.movedAtMs(), nowMs);
  }
}

// A label too long for its row scrolls, after a pause once the entry has settled.
void LauncherView::drawEntry(Canvas& canvas, const Entry& entry, const GfxFont& font, int x, int y,
                             int width, float light, bool scroll, int64_t sinceMs, int64_t nowMs) {
  int areaX = x;
  int areaW = width;
  if (canvas.width() >= kGlyphMinWidth) {
    glyph(canvas, entry, x, y + kGlyphTop, scale(glyphColor(entry), light), nowMs);
    areaX += kGlyphSpace;
    areaW -= kGlyphSpace;
  }
  const uint32_t color = scale(labelColor(entry), light);
  const int textW = text::width(font, entry.label);
  const int baseline = y + kBaseline;
  if (textW <= areaW || !scroll) {
    text::drawText(canvas, font, areaX, baseline, entry.label, color);
    return;
  }
  const ClipScope area(canvas, areaX, y, areaW, kRow);
  int offset = 0;
  if (nowMs - sinceMs > kScrollPauseMs) {
    const float travelled = static_cast<float>(nowMs - sinceMs - kScrollPauseMs) * kScrollPxPerSec / 1000.0f;
    offset = static_cast<int>(travelled) % (textW + kScrollGap);
  }
  text::drawText(canvas, font, areaX - offset, baseline, entry.label, color);
  if (offset > 0) text::drawText(canvas, font, areaX - offset + textW + kScrollGap, baseline, entry.label, color);
}

// Along the bottom row of a one-row panel: where the entry sits among the others.
void LauncherView::drawSlider(Canvas& canvas, const Launcher& menu, int y) {
  const int count = static_cast<int>(menu.entries().size());
  if (count < 2) return;
  const int trackW = canvas.width();
  const int thumb = std::max(2, trackW / count);
  const int x = (trackW - thumb) * menu.selected() / (count - 1);
  canvas.fillRect(0, y, trackW, 1, kTrack);
  canvas.fillRect(x, y, thumb, 1, kAmber);
}

}
