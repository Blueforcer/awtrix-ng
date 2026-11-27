#include "platform/host/ZoneRule.h"

#include <algorithm>

namespace awtrix::os {
namespace {

constexpr int64_t kDay = 86400;

bool digit(char c) { return c >= '0' && c <= '9'; }
bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

int64_t floorDiv(int64_t value, int64_t by) {
  const int64_t quotient = value / by;
  return quotient * by > value ? quotient - 1 : quotient;
}

bool leap(int64_t year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int daysIn(int64_t year, int month) {
  static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return month == 2 && leap(year) ? 29 : kDays[month - 1];
}

int64_t daysFromCivil(int64_t year, int month, int day) {
  year -= month <= 2;
  const int64_t era = floorDiv(year, 400);
  const int64_t yearOfEra = year - era * 400;
  const int64_t dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + dayOfEra - 719468;
}

void civilFromDays(int64_t days, int& year, int& month, int& day) {
  days += 719468;
  const int64_t era = floorDiv(days, 146097);
  const int64_t dayOfEra = days - era * 146097;
  const int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const int64_t shifted = (5 * dayOfYear + 2) / 153;
  day = static_cast<int>(dayOfYear - (153 * shifted + 2) / 5 + 1);
  month = static_cast<int>(shifted < 10 ? shifted + 3 : shifted - 9);
  year = static_cast<int>(yearOfEra + era * 400 + (month <= 2));
}

int weekdayOf(int64_t days) { return static_cast<int>(((days % 7) + 11) % 7); }

bool takeNumber(std::string_view& text, unsigned& value) {
  if (text.empty() || !digit(text[0])) return false;
  unsigned long number = 0;
  std::size_t at = 0;
  for (; at < text.size() && digit(text[at]); ++at)
    number = std::min<unsigned long>(number * 10 + static_cast<unsigned>(text[at] - '0'), 100000);
  text.remove_prefix(at);
  value = static_cast<unsigned>(number);
  return true;
}

bool takePart(std::string_view& text, char separator, unsigned& value) {
  if (text.size() < 2 || text[0] != separator || !digit(text[1])) return false;
  text.remove_prefix(1);
  return takeNumber(text, value);
}

bool takeClock(std::string_view& text, unsigned& hours, unsigned& minutes, unsigned& seconds) {
  minutes = seconds = 0;
  if (!takeNumber(text, hours)) return false;
  if (takePart(text, ':', minutes)) takePart(text, ':', seconds);
  return true;
}

bool takeName(std::string_view& text) {
  std::size_t at = 0;
  if (!text.empty() && text[0] == '<') {
    for (at = 1; at < text.size() && (digit(text[at]) || alpha(text[at]) || text[at] == '+' || text[at] == '-');)
      ++at;
    if (at >= text.size() || text[at] != '>' || at < 4) return false;
    text.remove_prefix(at + 1);
    return true;
  }
  while (at < text.size() && alpha(text[at])) ++at;
  if (at < 3) return false;
  text.remove_prefix(at);
  return true;
}

bool takeOffset(std::string_view& text, int32_t& offset) {
  std::string_view rest = text;
  int32_t sign = -1;
  if (!rest.empty() && (rest[0] == '+' || rest[0] == '-')) {
    sign = rest[0] == '-' ? 1 : -1;
    rest.remove_prefix(1);
  }
  unsigned hours = 0, minutes = 0, seconds = 0;
  if (!takeClock(rest, hours, minutes, seconds)) return false;
  offset = sign * static_cast<int32_t>(std::min(seconds, 59u) + std::min(minutes, 59u) * 60 +
                                       std::min(hours, 24u) * 3600);
  text = rest;
  return true;
}

}

bool ZoneRule::parseChange(std::string_view& text, Change& change) {
  if (!text.empty() && text[0] == ',') text.remove_prefix(1);
  if (text.empty()) return true;
  Change next;
  if (text[0] == 'J' || digit(text[0])) {
    const bool julian = text[0] == 'J';
    if (julian) text.remove_prefix(1);
    unsigned day = 0;
    if (!takeNumber(text, day) || day > 365 || (julian && day == 0)) return false;
    next.kind = julian ? Change::Kind::Julian : Change::Kind::Ordinal;
    next.day = static_cast<uint16_t>(day);
  } else if (text[0] == 'M') {
    text.remove_prefix(1);
    unsigned month = 0, week = 0, weekday = 0;
    if (!takeNumber(text, month) || !takePart(text, '.', week) || !takePart(text, '.', weekday) || month < 1 ||
        month > 12 || week < 1 || week > 5 || weekday > 6)
      return false;
    next.kind = Change::Kind::Month;
    next.month = static_cast<uint8_t>(month);
    next.week = static_cast<uint8_t>(week);
    next.day = static_cast<uint16_t>(weekday);
  } else {
    return false;
  }
  if (!text.empty() && text[0] != '/' && text[0] != ',') return false;
  if (!text.empty() && text[0] == '/') {
    text.remove_prefix(1);
    if (text.empty()) return false;
    const bool negative = text[0] == '-';
    if (negative) text.remove_prefix(1);
    unsigned hours = 2, minutes = 0, seconds = 0;
    if (!takeClock(text, hours, minutes, seconds)) hours = 2;
    next.seconds = (negative ? -1 : 1) * static_cast<int32_t>(hours * 3600 + minutes * 60 + seconds);
  }
  change = next;
  return true;
}

bool ZoneRule::parse(std::string_view rule) {
  *this = ZoneRule{};
  std::string_view text = rule;
  int32_t standard = 0;
  if (!takeName(text) || !takeOffset(text, standard)) return false;
  std_ = dst_ = standard;
  if (text.empty()) return true;
  if (!takeName(text)) return false;
  int32_t daylight = standard + 3600;
  takeOffset(text, daylight);
  Change start, end;
  start.month = 3;
  start.week = 2;
  end.month = 11;
  end.week = 1;
  if (!parseChange(text, start) || !parseChange(text, end)) return false;
  dst_ = daylight;
  start_ = start;
  end_ = end;
  hasDst_ = true;
  return true;
}

int64_t ZoneRule::changeAt(const Change& change, int year, int32_t offset) {
  int64_t day = daysFromCivil(year, 1, 1);
  switch (change.kind) {
    case Change::Kind::Julian:
      day += change.day - 1;
      if (change.day >= 60 && leap(year)) ++day;
      break;
    case Change::Kind::Ordinal:
      day += change.day;
      break;
    case Change::Kind::Month: {
      const int64_t first = daysFromCivil(year, change.month, 1);
      const int length = daysIn(year, change.month);
      int date = (change.day - weekdayOf(first) + 7) % 7;
      for (int week = 1; week < change.week && date + 7 < length; ++week) date += 7;
      day = first + date;
      break;
    }
  }
  return day * kDay + change.seconds - offset;
}

int32_t ZoneRule::offsetAt(int64_t utcSeconds, bool* dst) const {
  bool on = false;
  if (hasDst_) {
    int year = 0, month = 0, day = 0;
    civilFromDays(floorDiv(utcSeconds, kDay), year, month, day);
    const int64_t start = changeAt(start_, year, std_);
    const int64_t end = changeAt(end_, year, dst_);
    on = start > end ? (utcSeconds < end || utcSeconds >= start) : (utcSeconds >= start && utcSeconds < end);
  }
  if (dst) *dst = on;
  return on ? dst_ : std_;
}

LocalTime ZoneRule::local(int64_t utcSeconds) const {
  LocalTime out;
  out.offsetSeconds = offsetAt(utcSeconds, &out.dst);
  const int64_t shifted = utcSeconds + out.offsetSeconds;
  const int64_t days = floorDiv(shifted, kDay);
  const int64_t clock = shifted - days * kDay;
  civilFromDays(days, out.year, out.month, out.day);
  out.hour = static_cast<int>(clock / 3600);
  out.minute = static_cast<int>(clock / 60 % 60);
  out.second = static_cast<int>(clock % 60);
  out.weekday = weekdayOf(days);
  out.yearDay = static_cast<int>(days - daysFromCivil(out.year, 1, 1));
  return out;
}

}
