#include "core/apps/ClockText.h"

#include <cmath>
#include <cstdio>

namespace awtrix {

namespace {

const char* const kWeekdays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::string two(int v) {
  char b[8];
  std::snprintf(b, sizeof(b), "%02d", v);
  return b;
}

char sepChar(int style) {
  return style == kDateSepSlash ? '/' : style == kDateSepDash ? '-' : '.';
}

}

std::vector<TextRun> buildTimeRuns(const TimeTextOptions& o, int hour, int minute, int second) {
  int h = hour;
  if (!o.use24h) {
    h = hour % 12;
    if (h == 0) h = 12;
  }
  std::vector<TextRun> runs;
  runs.push_back({o.leadingZero ? two(h) : std::to_string(h), false});
  runs.push_back({":", true});
  runs.push_back({two(minute), false});
  if (o.showSeconds) {
    runs.push_back({":", true});
    runs.push_back({two(second), false});
  } else if (o.showAmPm && !o.use24h) {
    runs.push_back({hour < 12 ? " AM" : " PM", false});
  }
  return runs;
}

// Brightness of the ":" from 0 to 1. Blink alternates each second, pulse is a two second cosine.
float separatorLevel(int mode, int second, int64_t nowMs) {
  switch (mode) {
    case kSepBlink:
      return (second % 2) ? 0.0f : 1.0f;
    case kSepPulse: {
      const float phase = static_cast<float>(nowMs % 2000) / 2000.0f;
      return 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * phase));
    }
    default:
      return 1.0f;
  }
}

uint32_t scaleColor(uint32_t color, float level) {
  if (level >= 1.0f) return color;
  if (level <= 0.0f) return 0u;
  const auto ch = [&](int shift) {
    const float v = static_cast<float>((color >> shift) & 0xFFu) * level;
    return static_cast<uint32_t>(v + 0.5f) << shift;
  };
  return ch(16) | ch(8) | ch(0);
}

std::string buildDateText(const Settings& s, int weekday, int mday, int month, int year) {
  std::string out;
  if (s.dateShowWeekday && weekday >= 0 && weekday < 7) {
    out += kWeekdays[weekday];
    out += ' ';
  }
  const std::string day = two(mday);
  const std::string yr = s.dateYearMode == kYearFourDigit ? std::to_string(year)
                         : s.dateYearMode == kYearTwoDigit ? two(year % 100)
                                                           : "";
  if (s.dateMonthNames) {
    const std::string mon = (month >= 1 && month <= 12) ? kMonths[month - 1] : "?";
    switch (s.dateOrder) {
      case kDateOrderMDY: out += mon + ' ' + day; break;
      case kDateOrderYMD: out += (yr.empty() ? "" : yr + ' ') + mon + ' ' + day; return out;
      default: out += day + ' ' + mon; break;
    }
    if (!yr.empty()) out += ' ' + yr;
    return out;
  }
  const char sep = sepChar(s.dateSeparator);
  const std::string mon = two(month);
  switch (s.dateOrder) {
    case kDateOrderMDY:
      out += mon + sep + day;
      if (!yr.empty()) out += sep + yr;
      break;
    case kDateOrderYMD:
      if (!yr.empty()) out += yr + sep;
      out += mon + sep + day;
      break;
    default:
      out += day + sep + mon;
      if (!yr.empty()) out += sep + yr;
      // Day-month with dots keeps its trailing dot when the year is hidden: "24.12."
      else if (sep == '.') out += '.';
      break;
  }
  return out;
}

}
