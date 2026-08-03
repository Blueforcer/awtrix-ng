#include <unity.h>

#include <string>

#include "core/payload/ScrollSpec.h"

using namespace awtrix;

namespace {

ScrollSpec parsed(const char* json, bool* ok = nullptr, scroll::Error* err = nullptr) {
  ScrollSpec s;
  scroll::Error local;
  const bool r = scroll::read(api::JsonReader(json), s, err ? *err : local);
  if (ok) *ok = r;
  return s;
}

}

void setUp() {}
void tearDown() {}

static void test_records_a_field_that_is_present() {
  const ScrollSpec s = parsed(R"({"mode":"bounce"})");
  TEST_ASSERT_TRUE(s.hasMode);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScrollMode::Bounce), static_cast<int>(s.mode));
}

static void test_leaves_an_omitted_field_unset() {
  const ScrollSpec s = parsed(R"({"mode":"bounce"})");
  TEST_ASSERT_FALSE(s.hasSpeed);
  TEST_ASSERT_FALSE(s.hasGap);
  TEST_ASSERT_FALSE(s.hasDirection);
  TEST_ASSERT_FALSE(s.hasEntry);
  TEST_ASSERT_FALSE(s.hasWhenFits);
  TEST_ASSERT_FALSE(s.hasHoldMs);
}

static void test_string_shorthand_expands_to_mode() {
  const ScrollSpec s = parsed(R"("bounce")");
  TEST_ASSERT_TRUE(s.hasMode);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScrollMode::Bounce), static_cast<int>(s.mode));
  TEST_ASSERT_FALSE(s.hasSpeed);
}

static void test_reads_every_field() {
  const ScrollSpec s = parsed(
      R"({"mode":"loop","direction":"right","entry":"offscreen","whenFits":"scroll",)"
      R"("speed":40,"gap":3,"holdMs":250})");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScrollMode::Loop), static_cast<int>(s.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScrollDirection::Right), static_cast<int>(s.direction));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScrollEntry::Offscreen), static_cast<int>(s.entry));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ScrollWhenFits::Scroll), static_cast<int>(s.whenFits));
  TEST_ASSERT_EQUAL_INT(40, s.speed);
  TEST_ASSERT_EQUAL_INT(3, s.gap);
  TEST_ASSERT_EQUAL_INT(250, s.holdMs);
  TEST_ASSERT_TRUE(s.hasDirection && s.hasEntry && s.hasWhenFits && s.hasSpeed && s.hasGap &&
                   s.hasHoldMs);
}

static void rejects(const char* json, const char* field) {
  bool ok = true;
  scroll::Error err;
  parsed(json, &ok, &err);
  TEST_ASSERT_FALSE_MESSAGE(ok, json);
  TEST_ASSERT_EQUAL_STRING(field, err.field.c_str());
}

static void test_rejects_an_unknown_field() {
  rejects(R"({"speeed":50})", "scroll.speeed");
}

static void test_rejects_an_unknown_enum_value() {
  rejects(R"({"mode":"pingpong"})", "scroll.mode");
  rejects(R"({"direction":"up"})", "scroll.direction");
}

static void test_rejects_an_unknown_shorthand() {
  rejects(R"("pingpong")", "scroll");
}

static void test_rejects_a_negative_count() {
  rejects(R"({"speed":-1})", "scroll.speed");
  rejects(R"({"gap":-4})", "scroll.gap");
  rejects(R"({"holdMs":-1})", "scroll.holdMs");
}

static void test_rejects_a_non_integer_count() {
  rejects(R"({"speed":true})", "scroll.speed");
  rejects(R"({"gap":"wide"})", "scroll.gap");
  rejects(R"({"holdMs":1.5})", "scroll.holdMs");
}

static void test_rejects_a_value_that_is_neither_object_nor_string() {
  rejects(R"(42)", "scroll");
}

static void test_defaults_round_trip_through_json() {
  ScrollDefaults d;
  d.mode = ScrollMode::Loop;
  d.direction = ScrollDirection::Right;
  d.entry = ScrollEntry::Offscreen;
  d.whenFits = ScrollWhenFits::Scroll;
  d.speed = 55;
  d.gap = 2;
  d.holdMs = 400;

  std::string body;
  api::JsonWriter w(body);
  scroll::write(w, d);

  ScrollSpec back;
  scroll::Error err;
  TEST_ASSERT_TRUE_MESSAGE(scroll::read(api::JsonReader(body), back, err), err.field.c_str());
  TEST_ASSERT_TRUE(back.hasMode && back.hasDirection && back.hasEntry && back.hasWhenFits &&
                   back.hasSpeed && back.hasGap && back.hasHoldMs);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(d.mode), static_cast<int>(back.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(d.direction), static_cast<int>(back.direction));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(d.entry), static_cast<int>(back.entry));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(d.whenFits), static_cast<int>(back.whenFits));
  TEST_ASSERT_EQUAL_INT(d.speed, back.speed);
  TEST_ASSERT_EQUAL_INT(d.gap, back.gap);
  TEST_ASSERT_EQUAL_INT(d.holdMs, back.holdMs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_records_a_field_that_is_present);
  RUN_TEST(test_leaves_an_omitted_field_unset);
  RUN_TEST(test_string_shorthand_expands_to_mode);
  RUN_TEST(test_reads_every_field);
  RUN_TEST(test_rejects_an_unknown_field);
  RUN_TEST(test_rejects_an_unknown_enum_value);
  RUN_TEST(test_rejects_an_unknown_shorthand);
  RUN_TEST(test_rejects_a_negative_count);
  RUN_TEST(test_rejects_a_non_integer_count);
  RUN_TEST(test_rejects_a_value_that_is_neither_object_nor_string);
  RUN_TEST(test_defaults_round_trip_through_json);
  return UNITY_END();
}
