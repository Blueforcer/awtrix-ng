#include <unity.h>

#include "core/apps/ClockText.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static std::string joined(const std::vector<TextRun>& runs) {
  std::string s;
  for (const TextRun& r : runs) s += r.text;
  return s;
}

static void test_time_24h_default() {
  const auto runs = buildTimeRuns({true, false, false, true}, 9, 5, 0);
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(runs.size()));
  TEST_ASSERT_EQUAL_STRING("09", runs[0].text.c_str());
  TEST_ASSERT_FALSE(runs[0].separator);
  TEST_ASSERT_EQUAL_STRING(":", runs[1].text.c_str());
  TEST_ASSERT_TRUE(runs[1].separator);
  TEST_ASSERT_EQUAL_STRING("05", runs[2].text.c_str());
}

static void test_time_no_leading_zero() {
  const auto runs = buildTimeRuns({true, false, false, false}, 9, 5, 0);
  TEST_ASSERT_EQUAL_STRING("9:05", joined(runs).c_str());
}

static void test_time_12h_conversion_and_ampm() {
  TEST_ASSERT_EQUAL_STRING("12:00 AM", joined(buildTimeRuns({false, false, true, true}, 0, 0, 0)).c_str());
  TEST_ASSERT_EQUAL_STRING("12:30 PM", joined(buildTimeRuns({false, false, true, true}, 12, 30, 0)).c_str());
  TEST_ASSERT_EQUAL_STRING("01:15 PM", joined(buildTimeRuns({false, false, true, true}, 13, 15, 0)).c_str());
  TEST_ASSERT_EQUAL_STRING("1:15 PM", joined(buildTimeRuns({false, false, true, false}, 13, 15, 0)).c_str());
  TEST_ASSERT_EQUAL_STRING("13:15", joined(buildTimeRuns({true, false, true, true}, 13, 15, 0)).c_str());
}

static void test_time_seconds() {
  const auto runs = buildTimeRuns({true, true, false, true}, 8, 4, 7);
  TEST_ASSERT_EQUAL_INT(5, static_cast<int>(runs.size()));
  TEST_ASSERT_EQUAL_STRING("08:04:07", joined(runs).c_str());
  TEST_ASSERT_TRUE(runs[3].separator);
  TEST_ASSERT_EQUAL_STRING("08:04:07", joined(buildTimeRuns({false, true, true, true}, 8, 4, 7)).c_str());
}

static void test_separator_level() {
  TEST_ASSERT_EQUAL_FLOAT(1.0f, separatorLevel(kSepSteady, 1, 500));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, separatorLevel(kSepBlink, 4, 0));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, separatorLevel(kSepBlink, 5, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, separatorLevel(kSepPulse, 0, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, separatorLevel(kSepPulse, 0, 500));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, separatorLevel(kSepPulse, 0, 1000));
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, separatorLevel(kSepPulse, 0, 1500));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, separatorLevel(kSepPulse, 0, 2000));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, separatorLevel(kSepPulse, 0, 5000));
}

static void test_scale_color() {
  TEST_ASSERT_EQUAL_HEX32(0xFF8040u, scaleColor(0xFF8040u, 1.0f));
  TEST_ASSERT_EQUAL_HEX32(0x000000u, scaleColor(0xFF8040u, 0.0f));
  TEST_ASSERT_EQUAL_HEX32(0x804020u, scaleColor(0xFF8040u, 0.5f));
}

static Settings dateSettings(int order, int sep, int yearMode, bool weekday, bool monthNames) {
  Settings s;
  s.dateOrder = order;
  s.dateSeparator = sep;
  s.dateYearMode = yearMode;
  s.dateShowWeekday = weekday;
  s.dateMonthNames = monthNames;
  return s;
}

static void test_date_numeric() {
  Settings s = dateSettings(kDateOrderDMY, kDateSepDot, kYearTwoDigit, false, false);
  TEST_ASSERT_EQUAL_STRING("31.12.25", buildDateText(s, 3, 31, 12, 2025).c_str());
  s = dateSettings(kDateOrderMDY, kDateSepSlash, kYearTwoDigit, false, false);
  TEST_ASSERT_EQUAL_STRING("12/31/25", buildDateText(s, 3, 31, 12, 2025).c_str());
  s = dateSettings(kDateOrderYMD, kDateSepDash, kYearFourDigit, false, false);
  TEST_ASSERT_EQUAL_STRING("2025-12-31", buildDateText(s, 3, 31, 12, 2025).c_str());
  s = dateSettings(kDateOrderDMY, kDateSepDot, kYearNone, false, false);
  TEST_ASSERT_EQUAL_STRING("31.12.", buildDateText(s, 3, 31, 12, 2025).c_str());
  s = dateSettings(kDateOrderMDY, kDateSepSlash, kYearNone, false, false);
  TEST_ASSERT_EQUAL_STRING("12/31", buildDateText(s, 3, 31, 12, 2025).c_str());
}

static void test_date_weekday_and_month_names() {
  Settings s = dateSettings(kDateOrderDMY, kDateSepDot, kYearTwoDigit, true, false);
  TEST_ASSERT_EQUAL_STRING("Wed 31.12.25", buildDateText(s, 3, 31, 12, 2025).c_str());
  s = dateSettings(kDateOrderDMY, kDateSepDot, kYearNone, false, true);
  TEST_ASSERT_EQUAL_STRING("31 Dec", buildDateText(s, 3, 31, 12, 2025).c_str());
  s = dateSettings(kDateOrderMDY, kDateSepDot, kYearTwoDigit, false, true);
  TEST_ASSERT_EQUAL_STRING("Dec 31 25", buildDateText(s, 3, 31, 12, 2025).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_time_24h_default);
  RUN_TEST(test_time_no_leading_zero);
  RUN_TEST(test_time_12h_conversion_and_ampm);
  RUN_TEST(test_time_seconds);
  RUN_TEST(test_separator_level);
  RUN_TEST(test_scale_color);
  RUN_TEST(test_date_numeric);
  RUN_TEST(test_date_weekday_and_month_names);
  return UNITY_END();
}
