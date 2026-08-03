#include <unity.h>

#include <string>

#include "core/apps/builtin/WeekdayBar.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

using namespace awtrix;

namespace {

constexpr uint32_t kWorkActive = 0xFFFFFFu;
constexpr uint32_t kWorkIdle = 0x666666u;
constexpr uint32_t kEndActive = 0x00FF00u;
constexpr uint32_t kEndIdle = 0x006600u;

constexpr int kSunday = 0, kMonday = 1, kWednesday = 3, kSaturday = 6;

WeekdayBarConfig cfg(bool startOnMonday = true) {
  WeekdayBarConfig c;
  c.startOnMonday = startOnMonday;
  c.activeColor = kWorkActive;
  c.inactiveColor = kWorkIdle;
  c.weekendActiveColor = kEndActive;
  c.weekendInactiveColor = kEndIdle;
  return c;
}

}

void setUp() {}
void tearDown() {}

static void test_columns_map_to_calendar_days_starting_monday() {
  const WeekdayBarConfig c = cfg(true);
  TEST_ASSERT_EQUAL_INT(kMonday, weekdayBarCalendarDay(c, 0));
  TEST_ASSERT_EQUAL_INT(kSaturday, weekdayBarCalendarDay(c, 5));
  TEST_ASSERT_EQUAL_INT(kSunday, weekdayBarCalendarDay(c, 6));
}

static void test_columns_map_to_calendar_days_starting_sunday() {
  const WeekdayBarConfig c = cfg(false);
  TEST_ASSERT_EQUAL_INT(kSunday, weekdayBarCalendarDay(c, 0));
  TEST_ASSERT_EQUAL_INT(kMonday, weekdayBarCalendarDay(c, 1));
  TEST_ASSERT_EQUAL_INT(kSaturday, weekdayBarCalendarDay(c, 6));
}

static void test_the_four_states() {
  const WeekdayBarConfig c = cfg(true);
  TEST_ASSERT_EQUAL_HEX32(kWorkActive, weekdayBarColor(c, 2, kWednesday));
  TEST_ASSERT_EQUAL_HEX32(kWorkIdle, weekdayBarColor(c, 0, kWednesday));
  TEST_ASSERT_EQUAL_HEX32(kEndIdle, weekdayBarColor(c, 5, kWednesday));
  TEST_ASSERT_EQUAL_HEX32(kEndIdle, weekdayBarColor(c, 6, kWednesday));
  TEST_ASSERT_EQUAL_HEX32(kEndActive, weekdayBarColor(c, 5, kSaturday));
}

static void test_weekend_follows_the_calendar_not_the_column() {
  const WeekdayBarConfig c = cfg(false);
  TEST_ASSERT_EQUAL_HEX32(kEndIdle, weekdayBarColor(c, 0, kWednesday));
  TEST_ASSERT_EQUAL_HEX32(kEndIdle, weekdayBarColor(c, 6, kWednesday));
  TEST_ASSERT_EQUAL_HEX32(kWorkIdle, weekdayBarColor(c, 1, kWednesday));
  TEST_ASSERT_EQUAL_HEX32(kEndActive, weekdayBarColor(c, 0, kSunday));
}

static void test_an_empty_weekend_makes_every_day_a_workday() {
  WeekdayBarConfig c = cfg(true);
  c.weekendMask = 0;
  TEST_ASSERT_EQUAL_HEX32(kWorkIdle, weekdayBarColor(c, 5, kWednesday));
  TEST_ASSERT_EQUAL_HEX32(kWorkActive, weekdayBarColor(c, 5, kSaturday));
}

static void test_a_custom_weekend_is_honoured() {
  WeekdayBarConfig c = cfg(true);
  c.weekendMask = (1u << 5) | (1u << 6);
  TEST_ASSERT_EQUAL_HEX32(kEndIdle, weekdayBarColor(c, 4, kWednesday));
  TEST_ASSERT_EQUAL_HEX32(kWorkIdle, weekdayBarColor(c, 6, kWednesday));
}

static bool apply(const char* json, WeekdayBarConfig& into, weekdaybar::Error* err = nullptr) {
  weekdaybar::Error local;
  return weekdaybar::read(api::JsonReader(json), into, err ? *err : local);
}

static std::string written(const WeekdayBarConfig& c) {
  std::string out;
  api::JsonWriter w(out);
  weekdaybar::write(w, c);
  return out;
}

static void rejects(const char* json, const char* field) {
  WeekdayBarConfig c;
  weekdaybar::Error err;
  TEST_ASSERT_FALSE_MESSAGE(apply(json, c, &err), json);
  TEST_ASSERT_EQUAL_STRING(field, err.field.c_str());
}

static void test_round_trips_through_json() {
  WeekdayBarConfig src;
  src.show = false;
  src.startOnMonday = false;
  src.weekendMask = (1u << 5) | (1u << 6);
  src.activeColor = 0x112233u;
  src.inactiveColor = 0x445566u;
  src.weekendActiveColor = 0x778899u;
  src.weekendInactiveColor = 0xAABBCCu;

  const std::string body = written(src);
  WeekdayBarConfig back;
  weekdaybar::Error err;
  TEST_ASSERT_TRUE_MESSAGE(apply(body.c_str(), back, &err), err.field.c_str());
  TEST_ASSERT_EQUAL_INT(src.show, back.show);
  TEST_ASSERT_EQUAL_INT(src.startOnMonday, back.startOnMonday);
  TEST_ASSERT_EQUAL_UINT8(src.weekendMask, back.weekendMask);
  TEST_ASSERT_EQUAL_HEX32(src.activeColor, back.activeColor);
  TEST_ASSERT_EQUAL_HEX32(src.inactiveColor, back.inactiveColor);
  TEST_ASSERT_EQUAL_HEX32(src.weekendActiveColor, back.weekendActiveColor);
  TEST_ASSERT_EQUAL_HEX32(src.weekendInactiveColor, back.weekendInactiveColor);
}

static void test_a_partial_object_leaves_the_rest_alone() {
  WeekdayBarConfig c;
  c.activeColor = 0x123456u;
  TEST_ASSERT_TRUE(apply(R"({"weekendDays":["friday"]})", c));
  TEST_ASSERT_EQUAL_UINT8(1u << 5, c.weekendMask);
  TEST_ASSERT_EQUAL_HEX32(0x123456u, c.activeColor);
  TEST_ASSERT_TRUE(c.show);
}

static void test_an_empty_weekend_list_clears_the_mask() {
  WeekdayBarConfig c;
  TEST_ASSERT_TRUE(apply(R"({"weekendDays":[]})", c));
  TEST_ASSERT_EQUAL_UINT8(0u, c.weekendMask);
}

static void test_rejects_bad_input() {
  rejects(R"({"weekendDays":["caturday"]})", "weekdayBar.weekendDays");
  rejects(R"({"weekendDays":"saturday"})", "weekdayBar.weekendDays");
  rejects(R"({"startOnMonday":"yes"})", "weekdayBar.startOnMonday");
  rejects(R"({"activeColour":"#FFFFFF"})", "weekdayBar.activeColour");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_columns_map_to_calendar_days_starting_monday);
  RUN_TEST(test_columns_map_to_calendar_days_starting_sunday);
  RUN_TEST(test_the_four_states);
  RUN_TEST(test_weekend_follows_the_calendar_not_the_column);
  RUN_TEST(test_an_empty_weekend_makes_every_day_a_workday);
  RUN_TEST(test_a_custom_weekend_is_honoured);
  RUN_TEST(test_round_trips_through_json);
  RUN_TEST(test_a_partial_object_leaves_the_rest_alone);
  RUN_TEST(test_an_empty_weekend_list_clears_the_mask);
  RUN_TEST(test_rejects_bad_input);
  return UNITY_END();
}
