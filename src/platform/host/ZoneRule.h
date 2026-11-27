#pragma once

#include <cstdint>
#include <string_view>

namespace awtrix::os {

struct LocalTime {
  int year = 1970;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int weekday = 4;
  int yearDay = 0;
  int32_t offsetSeconds = 0;
  bool dst = false;
};

class ZoneRule {
 public:
  bool parse(std::string_view rule);
  LocalTime local(int64_t utcSeconds) const;
  int32_t offsetAt(int64_t utcSeconds, bool* dst = nullptr) const;
  bool changesOver() const { return hasDst_; }
  int64_t dstStarts(int year) const { return changeAt(start_, year, std_); }
  int64_t dstEnds(int year) const { return changeAt(end_, year, dst_); }

 private:
  struct Change {
    enum class Kind : uint8_t { Julian, Ordinal, Month };
    Kind kind = Kind::Month;
    uint16_t day = 0;
    uint8_t month = 1;
    uint8_t week = 1;
    int32_t seconds = 7200;
  };

  static bool parseChange(std::string_view& text, Change& change);
  static int64_t changeAt(const Change& change, int year, int32_t offset);

  int32_t std_ = 0;
  int32_t dst_ = 0;
  bool hasDst_ = false;
  Change start_{};
  Change end_{};
};

}
