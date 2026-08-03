#pragma once

#include "core/WeekdayBarConfig.h"
#include "core/render/Canvas.h"

namespace awtrix {

// Maps a bar column to a calendar day where 0 is Sunday, which is what weekendMask and the
// weekday in RenderCtx are numbered by.
inline int weekdayBarCalendarDay(const WeekdayBarConfig& c, int column) {
  return c.startOnMonday ? (column + 1) % 7 : column;
}

inline uint32_t weekdayBarColor(const WeekdayBarConfig& c, int column, int todayCalendarDay) {
  const int day = weekdayBarCalendarDay(c, column);
  const bool weekend = ((c.weekendMask >> day) & 1u) != 0u;
  const bool today = day == todayCalendarDay;
  if (weekend) return today ? c.weekendActiveColor : c.weekendInactiveColor;
  return today ? c.activeColor : c.inactiveColor;
}

inline void drawWeekdayBar(Canvas& c, const WeekdayBarConfig& cfg, int weekday, int x0,
                           int areaWidth, int width, int y) {
  const int span = 7 * width + 6;
  int start = x0 + (areaWidth - span) / 2;
  if (start < x0) start = x0;
  for (int i = 0; i < 7; ++i) {
    const int ls = start + i * (width + 1);
    c.drawLine(ls, y, ls + width - 1, y, weekdayBarColor(cfg, i, weekday));
  }
}

}
