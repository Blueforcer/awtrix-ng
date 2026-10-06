#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Settings.h"

namespace awtrix {

// Time text is split into runs so the ":" can be blinked or dimmed on its own.
struct TextRun {
  std::string text;
  bool separator = false;
};

struct TimeTextOptions {
  bool use24h = true;
  bool showSeconds = false;
  bool showAmPm = false;
  bool leadingZero = true;
};

inline TimeTextOptions timeOptionsFrom(const Settings& s) {
  return {s.time24h, s.timeShowSeconds, s.timeShowAmPm, s.timeLeadingZero};
}

std::vector<TextRun> buildTimeRuns(const TimeTextOptions& o, int hour, int minute, int second);

float separatorLevel(int mode, int second, int64_t nowMs);

uint32_t scaleColor(uint32_t color, float level);

std::string buildDateText(const Settings& s, int weekday, int mday, int month, int year);

// English three-letter names as the date text uses them; "?" outside the range. weekday counts
// from 0 = Sunday, month from 1 = January.
const char* weekdayShortName(int weekday);
const char* monthShortName(int month);

}
