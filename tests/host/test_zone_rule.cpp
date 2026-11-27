#include "platform/host/ZoneRule.h"
#include "../support.h"

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

using awtrix::os::LocalTime;
using awtrix::os::ZoneRule;
using awtrix::test::check;
using awtrix::test::require;

namespace {

int64_t epoch(int year, int month, int day, int hour = 0, int minute = 0, int second = 0) {
  const int y = year - (month <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
  return days * 86400 + hour * 3600 + minute * 60 + second;
}

std::string text(const LocalTime& t) {
  char line[96];
  std::snprintf(line, sizeof line, "%04d-%02d-%02d %02d:%02d:%02d %+d%s", t.year, t.month, t.day, t.hour,
                t.minute, t.second, static_cast<int>(t.offsetSeconds), t.dst ? " dst" : "");
  return line;
}

void expect(const char* rule, int64_t at, const char* wanted) {
  ZoneRule zone;
  zone.parse(rule);
  const std::string got = text(zone.local(at));
  check(got == wanted, std::string(rule) + " at " + std::to_string(at) + ": " + got + ", wanted " + wanted);
}

void fixedCases() {
  const char* cet = "CET-1CEST,M3.5.0,M10.5.0/3";
  expect(cet, epoch(2026, 3, 29, 0, 59, 59), "2026-03-29 01:59:59 +3600");
  expect(cet, epoch(2026, 3, 29, 1), "2026-03-29 03:00:00 +7200 dst");
  expect(cet, epoch(2026, 10, 25, 0, 59, 59), "2026-10-25 02:59:59 +7200 dst");
  expect(cet, epoch(2026, 10, 25, 1), "2026-10-25 02:00:00 +3600");
  const char* sydney = "AEST-10AEDT,M10.1.0,M4.1.0/3";
  expect(sydney, epoch(2026, 1, 15, 12), "2026-01-15 23:00:00 +39600 dst");
  expect(sydney, epoch(2026, 4, 4, 15, 59, 59), "2026-04-05 02:59:59 +39600 dst");
  expect(sydney, epoch(2026, 4, 4, 16), "2026-04-05 02:00:00 +36000");
  expect(sydney, epoch(2026, 10, 3, 15, 59, 59), "2026-10-04 01:59:59 +36000");
  expect(sydney, epoch(2026, 10, 3, 16), "2026-10-04 03:00:00 +39600 dst");
  const char* lordHowe = "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0";
  expect(lordHowe, epoch(2026, 4, 4, 14, 59, 59), "2026-04-05 01:59:59 +39600 dst");
  expect(lordHowe, epoch(2026, 4, 4, 15), "2026-04-05 01:30:00 +37800");
  const char* nuuk = "<-02>2<-01>,M3.5.0/-1,M10.5.0/0";
  expect(nuuk, epoch(2026, 3, 29, 0, 59, 59), "2026-03-28 22:59:59 -7200");
  expect(nuuk, epoch(2026, 3, 29, 1), "2026-03-29 00:00:00 -3600 dst");
  const char* santiago = "<-04>4<-03>,M9.1.6/24,M4.1.6/24";
  expect(santiago, epoch(2026, 9, 6, 3, 59, 59), "2026-09-05 23:59:59 -14400");
  expect(santiago, epoch(2026, 9, 6, 4), "2026-09-06 01:00:00 -10800 dst");
  const char* dublin = "IST-1GMT0,M10.5.0,M3.5.0/1";
  expect(dublin, epoch(2026, 7, 1, 12), "2026-07-01 13:00:00 +3600");
  expect(dublin, epoch(2026, 1, 15, 12), "2026-01-15 12:00:00 +0 dst");
  const char* eastern = "EST5EDT";
  expect(eastern, epoch(2026, 3, 8, 6, 59, 59), "2026-03-08 01:59:59 -18000");
  expect(eastern, epoch(2026, 3, 8, 7), "2026-03-08 03:00:00 -14400 dst");
  expect(eastern, epoch(2026, 11, 1, 5, 59, 59), "2026-11-01 01:59:59 -14400 dst");
  expect(eastern, epoch(2026, 11, 1, 6), "2026-11-01 01:00:00 -18000");
  expect("<+0545>-5:45", epoch(2026, 6, 1), "2026-06-01 05:45:00 +20700");
  expect("UTC0", epoch(1969, 12, 31, 23, 59, 59), "1969-12-31 23:59:59 +0");

  ZoneRule zone;
  check(!zone.parse(""), "an empty rule is refused");
  check(text(zone.local(epoch(2026, 6, 1, 12))) == "2026-06-01 12:00:00 +0", "a refused rule leaves UTC");
  check(!zone.parse("CE"), "a name of two letters is refused");
  check(!zone.parse("CET-1CEST,M13.1.0,M10.5.0"), "a thirteenth month is refused");
  check(!zone.parse("CET-1CEST,M3.6.0,M10.5.0"), "a sixth week is refused");
  check(!zone.parse("CET-1CEST,Q3,M10.5.0"), "an unknown change is refused");
  require(zone.parse("XXX0YYY,J60/0,J300/0"), "a Julian rule");
  check(zone.dstStarts(2028) == epoch(2028, 3, 1), "J60 is 1 March in a leap year");
  require(zone.parse("XXX0YYY,59/0,300/0"), "an ordinal rule");
  check(zone.dstStarts(2028) == epoch(2028, 2, 29), "day 59 is 29 February in a leap year");
  check(zone.dstStarts(2027) == epoch(2027, 3, 1), "day 59 is 1 March in other years");
  require(zone.parse(cet), "the CET rule");
  const LocalTime day = zone.local(epoch(2026, 11, 26, 12));
  check(day.weekday == 4 && day.yearDay == 329, "weekday and day of the year");
  check(zone.local(epoch(1969, 12, 31, 12)).weekday == 3, "weekday before 1970");
}

#if !defined(_WIN32)
struct Tally {
  unsigned rules = 0, dstRules = 0, samples = 0, edges = 0;
};

bool sameAsLibc(const ZoneRule& zone, const std::string& rule, int64_t at) {
  const std::time_t t = static_cast<std::time_t>(at);
  std::tm libc{};
  if (!::localtime_r(&t, &libc)) return false;
  const LocalTime ours = zone.local(at);
  const bool same = ours.year == libc.tm_year + 1900 && ours.month == libc.tm_mon + 1 && ours.day == libc.tm_mday &&
                    ours.hour == libc.tm_hour && ours.minute == libc.tm_min && ours.second == libc.tm_sec &&
                    ours.weekday == libc.tm_wday && ours.yearDay == libc.tm_yday &&
                    ours.offsetSeconds == libc.tm_gmtoff && ours.dst == (libc.tm_isdst > 0);
  if (!same) {
    char line[96];
    std::strftime(line, sizeof line, "%Y-%m-%d %H:%M:%S", &libc);
    check(false, rule + " at " + std::to_string(at) + ": ours " + text(ours) + ", glibc " + line + " " +
                     std::to_string(libc.tm_gmtoff) + (libc.tm_isdst > 0 ? " dst" : ""));
  }
  return same;
}

void againstLibc(const char* table, Tally& tally) {
  std::ifstream file(table);
  require(file.good(), "open the time zone table");
  std::vector<std::string> rules;
  for (std::string line; std::getline(file, line);) {
    if (line.rfind("['", 0) != 0) continue;
    const auto end = line.find('\'', 2);
    if (end != std::string::npos) rules.push_back(line.substr(2, end - 2));
  }
  require(rules.size() > 50, "the table lists the zones' rules");
  for (const std::string& rule : rules) {
    ZoneRule zone;
    check(zone.parse(rule), "parse " + rule);
#if defined(__APPLE__)
    if (rule.find("/-") != std::string::npos) continue;
#endif
    ::setenv("TZ", rule.c_str(), 1);
    ::tzset();
    ++tally.rules;
    unsigned wrong = 0;
    for (int64_t at = epoch(2000, 1, 1); at < epoch(2050, 1, 1) && wrong < 3; at += 14833) {
      ++tally.samples;
      if (!sameAsLibc(zone, rule, at)) ++wrong;
    }
    if (!zone.changesOver()) continue;
    ++tally.dstRules;
    for (int year = 2000; year < 2050 && wrong < 3; ++year) {
      for (const int64_t change : {zone.dstStarts(year), zone.dstEnds(year)}) {
        bool before = false, after = false;
        const int32_t was = zone.offsetAt(change - 1, &before);
        const int32_t now = zone.offsetAt(change, &after);
        check(before != after && was != now, rule + " changes at " + std::to_string(change));
        for (const int64_t at : {change - 1, change, change + 1}) {
          ++tally.edges;
          if (!sameAsLibc(zone, rule, at)) ++wrong;
        }
      }
    }
  }
  ::unsetenv("TZ");
  ::tzset();
}
#endif

}

int main(int argc, char** argv) {
  fixedCases();
#if !defined(_WIN32)
  require(argc == 2, "usage: zone-rule-test TIMEZONES_JS");
  Tally tally;
  againstLibc(argv[1], tally);
  std::printf("%u rules (%u with daylight saving), %u samples 2000-2049, %u change edges against glibc\n",
              tally.rules, tally.dstRules, tally.samples, tally.edges);
#else
  (void)argc;
  (void)argv;
#endif
  return awtrix::test::finish("zone-rule");
}
