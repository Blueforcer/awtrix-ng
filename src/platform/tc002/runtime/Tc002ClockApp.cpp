#include "platform/tc002/runtime/Tc002ClockApp.h"
#include "platform/tc002/contract/tc002_layout.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "core/Settings.h"
#include "core/apps/ClockText.h"
#include "core/apps/builtin/WeekdayBar.h"
#include "core/render/Color.h"
#include "core/render/FontCatalog.h"
#include "core/render/TextEncoding.h"
#include "core/render/TextRenderer.h"
#include "platform/tc002/runtime/Tc002Paint.h"

namespace awtrix {

namespace {

constexpr int kPanelW = TC002_PANEL_WIDTH;
constexpr int kSheetW = 15;
constexpr int kLabelRows = 7;
constexpr int kTimeX = 17, kTimeW = 34;
constexpr int kRowX = 1, kRowW = 50;
constexpr uint32_t kRingGrey = 0x9A9A9Au;
constexpr uint32_t kTodayBlue = 0x0050FFu;
// Output row -> glyph row. Doubling every row is plain 2x.
constexpr uint8_t kDoubleRows[10] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4};
constexpr int kFlapSplit = 8, kTileW = 16;
constexpr int kMinuteTileX = kPanelW - kTileW, kFlapColonX = kMinuteTileX - 2;
constexpr int64_t kFlipMs = 600;
constexpr int64_t kTearMs = 1100;
constexpr uint8_t kNone = 0xFF;
// The split: the upper five rows of a 2x digit, one dark row, the lower five.
constexpr uint8_t kFlapRows[11] = {0, 0, 1, 1, 2, kNone, 2, 3, 3, 4, 4};
// Doubling only the three bars of a 7-row digit gives the 10-row big-face digits with 2-pixel
// strokes throughout.
constexpr uint8_t kBarRows[10] = {0, 0, 1, 2, 3, 3, 4, 5, 6, 6};

struct Day {
  int year, month, mday, weekday;
};

struct Scene {
  const Settings& s;
  const GfxFont& small;
  const GfxFont& chunky;
  Day day;
  int hour, minute, second, ms;
  bool set;
  int64_t tearSinceMs;
  float sepLevel;
  uint32_t timeColor;
  int smallCellWidth, chunkyCellWidth;
  const std::string& time;
  const std::string& before;
};

enum class Head { Label, Rings, Band };

text::TextMetrics inkOf(const GfxFont& f, char ch) {
  return text::measureInk(f, std::string_view(&ch, 1));
}

// Draws ch with its ink starting at column x; see text::drawGlyphRows for kx and the row map.
void drawMapped(Canvas& c, const GfxFont& f, char ch, int x, int top, int rowTop, int kx,
                const uint8_t* rows, int nRows, uint32_t color) {
  text::drawGlyphRows(c, f, x - inkOf(f, ch).inkLeft * kx, top,
                      text::glyphFor(f, static_cast<unsigned char>(ch)), kx, rowTop, rows, nRows,
                      color);
}

// A time string on tabular cells: every character takes the cell of the widest digit, a colon
// keeps its own ink width. gap separates neighbours, colonGap stands on either side of a colon.
struct Cells {
  const GfxFont* font;
  int kx;
  const uint8_t* rows;
  int nRows;
  int gap, colonGap;
  int digitWidth;
};

int digitWidth(const GfxFont& font) {
  int width = 0;
  for (char d = '0'; d <= '9'; ++d) width = std::max(width, inkOf(font, d).inkWidth());
  return width;
}

int cellWidth(const Cells& k, char ch) {
  return (ch == ':' ? inkOf(*k.font, ch).inkWidth() : k.digitWidth) * k.kx;
}

int gapAfter(const Cells& k, const std::string& s, std::size_t i) {
  return s[i] == ':' || (i + 1 < s.size() && s[i + 1] == ':') ? k.colonGap : k.gap;
}

int cellsWidth(const Cells& k, const std::string& s) {
  int w = 0;
  for (std::size_t i = 0; i < s.size(); ++i)
    w += cellWidth(k, s[i]) + (i + 1 < s.size() ? gapAfter(k, s, i) : 0);
  return w;
}

// Rows are counted from the ink top of '0', so dashes and colons keep their height.
void drawCells(Canvas& c, const Cells& k, int x, int top, const std::string& s, uint32_t color,
               float sepLevel) {
  const int rowTop = inkOf(*k.font, '0').inkTop;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char ch = s[i];
    const int w = cellWidth(k, ch);
    const bool colon = ch == ':';
    if (!colon || sepLevel > 0.0f)
      drawMapped(c, *k.font, ch, x + (w - inkOf(*k.font, ch).inkWidth() * k.kx) / 2, top, rowTop,
                 k.kx, k.rows, k.nRows, colon ? scaleColor(color, sepLevel) : color);
    x += w + (i + 1 < s.size() ? gapAfter(k, s, i) : 0);
  }
}

std::string timeText(const Settings& s, int hour, int minute, int second, bool seconds) {
  std::string out;
  for (const TextRun& r : buildTimeRuns({s.time24h, seconds, false, s.timeLeadingZero}, hour,
                                        minute, second))
    out += r.text;
  return out;
}

int daysInMonth(int year, int month) {
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return month == 2 && leap ? 29 : kDays[(month + 11) % 12];
}

// One dot per day of the month, a week per row.
void drawMonthGrid(Canvas& c, const Scene& k, const Day& d, int headRows) {
  const Settings& s = k.s;
  const WeekdayBarConfig& wb = s.weekdayBar;
  const int first = ((d.weekday - (d.mday - 1)) % 7 + 7) % 7;
  const int col0 = (first - (wb.startOnMonday ? 1 : 0) + 7) % 7;
  const int n = daysInMonth(d.year, d.month);
  for (int day = 1; day <= n; ++day) {
    const int idx = col0 + day - 1;
    const int col = idx % 7, row = idx / 7;
    const bool weekend = ((wb.weekendMask >> weekdayBarCalendarDay(wb, col)) & 1u) != 0;
    uint32_t color = weekend ? s.calendarHeaderColor : s.calendarTextColor;
    if (day == d.mday) color = kTodayBlue;
    else if (day < d.mday) color = color::lerp(s.calendarBodyColor, color, 0.3f);
    c.setPixel(1 + col * 2, headRows + 1 + row * 2, color);
  }
}

// Label, rings or band above the month grid; other bodies show the day number.
void drawSheet(Canvas& c, const Scene& k, const Day& d, Head head, bool labelWeekday) {
  const Settings& s = k.s;
  const int headRows = head == Head::Label ? kLabelRows : head == Head::Rings ? 4 : 3;
  const int paperTop = head == Head::Rings ? 1 : 0;
  c.fillRect(0, paperTop, kSheetW, 16 - paperTop, s.calendarBodyColor);
  c.fillRect(0, paperTop, kSheetW, headRows - paperTop, s.calendarHeaderColor);
  if (head == Head::Rings)
    for (int x : {3, 10}) {
      c.fillRect(x, 0, 2, 2, kRingGrey);
      c.fillRect(x, 2, 2, 1, 0x000000u);
    }
  if (!k.set) return;
  if (head == Head::Label) {
    const std::string label =
        text::toUpperUtf8(labelWeekday ? weekdayShortName(d.weekday) : monthShortName(d.month));
    paint::drawInk(c, k.small, (kSheetW - text::measure(k.small, label).inkWidth()) / 2, 1, label,
                   s.calendarBodyColor);
  }
  if (head == Head::Band) return drawMonthGrid(c, k, d, headRows);
  const std::string num = std::to_string(d.mday);
  const text::TextMetrics m = text::measureInk(k.chunky, num);
  paint::drawInk(c, k.chunky, (kSheetW - m.inkWidth()) / 2,
                 headRows + (16 - headRows - m.inkHeight()) / 2, num, s.calendarTextColor);
}

Day previousDay(Day d) {
  d.weekday = (d.weekday + 6) % 7;
  if (--d.mday >= 1) return d;
  if (--d.month < 1) {
    d.month = 12;
    --d.year;
  }
  d.mday = daysInMonth(d.year, d.month);
  return d;
}

// Today's sheet; while a tear-off runs, yesterday's falls away over it.
void drawTornSheet(Canvas& c, const Scene& k, Head head, bool labelWeekday) {
  drawSheet(c, k, k.day, head, labelWeekday);
  if (k.tearSinceMs < 0 || k.tearSinceMs >= kTearMs) return;
  Canvas old(kSheetW, 16);
  drawSheet(old, k, previousDay(k.day), head, labelWeekday);
  const float p = static_cast<float>(k.tearSinceMs) / kTearMs;
  const int dy = static_cast<int>(17.0f * p * p + 0.5f);
  const int lean = static_cast<int>(3.0f * p * p + 0.5f);
  const int keep = head == Head::Rings ? 3 : 0;
  for (int y = 15; y >= keep; --y) {
    const int to = y + dy;
    if (to > 15) continue;
    for (int x = 0; x < kSheetW; ++x) c.setPixel(x + (to > 8 ? lean : 0), to, old.getPixel(x, y));
  }
}

void drawSideTime(Canvas& c, const Scene& k, bool bar) {
  const Cells cells{&k.small, 2, kDoubleRows, 10, 2, 2, k.smallCellWidth};
  const std::string& t = k.time;
  drawCells(c, cells, kTimeX + (kTimeW - cellsWidth(cells, t)) / 2, bar ? 2 : 3, t, k.timeColor,
            k.sepLevel);
  if (bar && k.set) drawWeekdayBar(c, k.s.weekdayBar, k.day.weekday, kTimeX, kTimeW, 4, 15);
}

void renderSheet(Canvas& c, const Scene& k) {
  const bool bar = k.s.weekdayBar.show;
  drawTornSheet(c, k, Head::Label, !bar);
  drawSideTime(c, k, bar);
}

void renderRing(Canvas& c, const Scene& k) {
  drawTornSheet(c, k, Head::Rings, false);
  drawSideTime(c, k, k.s.weekdayBar.show);
}

void renderMonth(Canvas& c, const Scene& k) {
  drawTornSheet(c, k, Head::Band, false);
  drawSideTime(c, k, false);
}

// A 16 x 10 image of a pair of digits on plain 2x rows, for the moving flap.
Canvas flapImage(const Scene& k, const std::string& pair) {
  Canvas img(kTileW, 10);
  const Cells cells{&k.small, 2, kDoubleRows, 10, 2, 2, k.smallCellWidth};
  drawCells(img, cells, (kTileW - cellsWidth(cells, pair)) / 2, 0, pair, k.timeColor, 1.0f);
  return img;
}

// One flap tile: the old upper half folds down, then the new lower half unfolds.
void drawFlapTile(Canvas& c, const Scene& k, int x0, const std::string& now,
                  const std::string& before, float p) {
  c.fillRect(x0, 0, kTileW, kFlapSplit, scaleColor(k.timeColor, 0.18f));
  c.fillRect(x0, kFlapSplit + 1, kTileW, 16 - kFlapSplit - 1, scaleColor(k.timeColor, 0.18f));
  if (p >= 1.0f || before == now) {
    const Cells cells{&k.small, 2, kFlapRows, 11, 2, 2, k.smallCellWidth};
    drawCells(c, cells, x0 + (kTileW - cellsWidth(cells, now)) / 2, kFlapSplit - 5, now,
              k.timeColor, 1.0f);
    return;
  }
  const uint32_t flap = scaleColor(k.timeColor, 0.28f);
  const Canvas a = flapImage(k, now);
  const Canvas b = flapImage(k, before);
  const int top = kFlapSplit - 5;
  for (int j = 0; j < 5; ++j)
    for (int i = 0; i < kTileW; ++i) {
      if (const uint32_t v = a.getPixel(i, j)) c.setPixel(x0 + i, top + j, v);
      if (const uint32_t v = b.getPixel(i, 5 + j)) c.setPixel(x0 + i, kFlapSplit + 1 + j, v);
    }
  if (p < 0.5f) {
    const int h = std::max(1, static_cast<int>(kFlapSplit * (1.0f - p / 0.5f) + 0.5f));
    for (int j = 0; j < h; ++j) {
      const int src = j * kFlapSplit / h - top;
      for (int i = 0; i < kTileW; ++i) {
        const uint32_t v = src >= 0 && src < 5 ? b.getPixel(i, src) : 0u;
        c.setPixel(x0 + i, kFlapSplit - h + j, v ? v : flap);
      }
    }
    return;
  }
  const int lower = 16 - kFlapSplit - 1;
  const int h = std::max(1, static_cast<int>(lower * (p - 0.5f) / 0.5f + 0.5f));
  for (int j = 0; j < h; ++j) {
    const int src = j * lower / h;
    for (int i = 0; i < kTileW; ++i) {
      const uint32_t v = src < 5 ? a.getPixel(i, 5 + src) : 0u;
      c.setPixel(x0 + i, kFlapSplit + 1 + j, v ? v : flap);
    }
  }
}

void renderFlap(Canvas& c, const Scene& k) {
  drawTornSheet(c, k, Head::Label, true);
  if (k.set) {
    const int since = k.second * 1000 + k.ms;
    const float p = since < kFlipMs ? static_cast<float>(since) / kFlipMs : 1.0f;
    const std::string& now = k.time;
    const std::string& before = k.before;
    const std::size_t cut = now.find(':'), cutBefore = before.find(':');
    drawFlapTile(c, k, kTimeX, now.substr(0, cut), before.substr(0, cutBefore), p);
    drawFlapTile(c, k, kMinuteTileX, now.substr(cut + 1), before.substr(cutBefore + 1), p);
  } else {
    drawFlapTile(c, k, kTimeX, "--", "--", 1.0f);
    drawFlapTile(c, k, kMinuteTileX, "--", "--", 1.0f);
  }
  if (k.sepLevel > 0.0f) {
    const uint32_t dot = scaleColor(k.timeColor, k.sepLevel);
    c.setPixel(kFlapColonX, 5, dot);
    c.setPixel(kFlapColonX, 11, dot);
  }
}

// buildDateText without the weekday prefix, in capitals; a date too wide for maxWidth loses the
// century of a four-digit year first, then the year.
std::string fittedDate(const Settings& s, const Day& d, int maxWidth, const GfxFont& small) {
  Settings v = s;
  v.dateShowWeekday = false;
  const int years[3] = {s.dateYearMode,
                        s.dateYearMode == kYearFourDigit ? kYearTwoDigit : s.dateYearMode, kYearNone};
  std::string out;
  for (int year : years) {
    v.dateYearMode = year;
    out = text::toUpperUtf8(buildDateText(v, d.weekday, d.mday, d.month, d.year));
    if (text::measure(small, out).inkWidth() <= maxWidth) break;
  }
  return out;
}

// The big face: time, then weekday and date.
void renderBig(Canvas& c, const Scene& k) {
  const bool secs = k.s.timeShowSeconds;
  const Cells cells = secs ? Cells{&k.small, 2, kDoubleRows, 10, 2, 1, k.smallCellWidth}
                           : Cells{&k.chunky, 2, kBarRows, 10, 2, 1, k.chunkyCellWidth};
  const std::string& t = k.time;
  drawCells(c, cells, (kPanelW - cellsWidth(cells, t)) / 2, 0, t, k.timeColor, k.sepLevel);
  if (!k.set) return;
  const std::string weekday = text::toUpperUtf8(weekdayShortName(k.day.weekday));
  const text::TextMetrics wm = text::measure(k.small, weekday);
  const std::string date = fittedDate(k.s, k.day, kRowW - wm.inkWidth() - 3, k.small);
  const text::TextMetrics dm = text::measure(k.small, date);
  text::drawText(c, k.small, kRowX - wm.inkLeft, 16, weekday, k.s.calendarHeaderColor);
  text::drawText(c, k.small, kRowX + kRowW - dm.inkWidth() - dm.inkLeft, 16, date,
                 k.s.dateColor.valueOr(k.s.textColor));
}

}

// Milliseconds into the running tear-off, or -1. While the clock slides in it shows the sheet that
// is about to fall (0). A tear starts when the clock is fully on screen, and when the date changes
// under it unless one is already running.
int64_t Tc002ClockApp::tearSince(const RenderCtx& ctx) {
  const int day = ctx.year * 10000 + ctx.month * 100 + ctx.mday;
  const bool newDay = day_ >= 0 && day != day_;
  day_ = day;
  if (ctx.shownSinceMs < 0) return 0;
  const bool running = tearStartMs_ >= 0 && ctx.nowMs - tearStartMs_ < kTearMs;
  if (ctx.shownSinceMs != shownSinceMs_) {
    shownSinceMs_ = ctx.shownSinceMs;
    tearStartMs_ = ctx.shownSinceMs;
  } else if (newDay && !running) {
    tearStartMs_ = ctx.nowMs;
  }
  const int64_t since = ctx.nowMs - tearStartMs_;
  return tearStartMs_ >= 0 && since < kTearMs ? since : -1;
}

// Flips changed digits from the last displayed minute. A minute seen after a gap starts still.
void Tc002ClockApp::trackFlap(const RenderCtx& ctx) {
  const int shown = ctx.hour * 60 + ctx.minute;
  if (shown != flapShown_) {
    const bool continuous = flapSeenMs_ >= 0 && ctx.nowMs - flapSeenMs_ < 1000;
    flapFrom_ = continuous ? flapShown_ : shown;
    flapShown_ = shown;
  }
  flapSeenMs_ = ctx.nowMs;
}

void Tc002ClockApp::cacheText(const RenderCtx& ctx) {
  const Settings& s = *ctx.settings;
  const bool seconds = s.clockFace == kClockFaceBig && s.timeShowSeconds;
  const bool set = ctx.epochMs >= 0;
  const int hour = set ? ctx.hour : -1, minute = set ? ctx.minute : -1;
  const int second = set && seconds ? ctx.second : -1;
  const bool formatChanged = text24h_ != s.time24h || textLeadingZero_ != s.timeLeadingZero;
  if (formatChanged) previousText_.clear();
  if (timeText_.empty() || formatChanged || textSeconds_ != seconds ||
      textHour_ != hour || textMinute_ != minute || textSecond_ != second) {
    timeText_ = set ? timeText(s, hour, minute, ctx.second, seconds)
                    : (seconds ? "--:--:--" : "--:--");
    textHour_ = hour;
    textMinute_ = minute;
    textSecond_ = second;
    textSeconds_ = seconds;
  }
  if (s.clockFace == kClockFaceFlap && set &&
      (previousText_.empty() || formatChanged || textFlapFrom_ != flapFrom_)) {
    previousText_ = timeText(s, flapFrom_ / 60, flapFrom_ % 60, 0, false);
    textFlapFrom_ = flapFrom_;
  }
  text24h_ = s.time24h;
  textLeadingZero_ = s.timeLeadingZero;
}

void Tc002ClockApp::render(Canvas& c, const RenderCtx& ctx) {
  const Settings& s = *ctx.settings;
  const GfxFont& small = *ctx.fonts->small().font;
  if (catalog_ != ctx.fonts) {
    catalog_ = ctx.fonts;
    const FontEntry* chunky = catalog_->find("matrix-chunky8x6");
    chunky_ = chunky ? chunky->font : &small;
    smallCellWidth_ = digitWidth(small);
    chunkyCellWidth_ = digitWidth(*chunky_);
  }
  const bool set = ctx.epochMs >= 0;
  if (set && s.clockFace == kClockFaceFlap) trackFlap(ctx);
  cacheText(ctx);
  // Tracked with the animation off too.
  const int64_t tear = set ? tearSince(ctx) : -1;
  const Scene k{s,
                small,
                *chunky_,
                Day{ctx.year, ctx.month, ctx.mday, ctx.weekday},
                ctx.hour,
                ctx.minute,
                ctx.second,
                set ? static_cast<int>(ctx.epochMs % 1000) : 0,
                set,
                s.calendarAnimation ? tear : -1,
                set ? separatorLevel(s.timeSeparatorMode, ctx.second, ctx.nowMs) : 1.0f,
                s.timeColor.valueOr(s.textColor),
                smallCellWidth_, chunkyCellWidth_, timeText_, previousText_};
  switch (s.clockFace) {
    case kClockFaceRing:
      renderRing(c, k);
      break;
    case kClockFaceMonth:
      renderMonth(c, k);
      break;
    case kClockFaceFlap:
      renderFlap(c, k);
      break;
    case kClockFaceBig:
      renderBig(c, k);
      break;
    default:
      renderSheet(c, k);
      break;
  }
}

}
