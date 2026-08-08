#include <unity.h>

#include <string>

#include "core/sound/SoundClips.h"

using namespace awtrix;

namespace {

void test_plain_names_map_into_sounds() {
  TEST_ASSERT_EQUAL_STRING("/CLIPS/ding.mp3", sound::clipPathFor("ding").c_str());
  TEST_ASSERT_EQUAL_STRING("/CLIPS/Alarm_2.mp3", sound::clipPathFor("Alarm_2").c_str());
  TEST_ASSERT_EQUAL_STRING("/CLIPS/a-b.mp3", sound::clipPathFor("a-b").c_str());
  TEST_ASSERT_EQUAL_STRING("/CLIPS/7.mp3", sound::clipPathFor("7").c_str());
}

void test_unusable_names_yield_empty() {
  TEST_ASSERT_TRUE(sound::clipPathFor("").empty());
  TEST_ASSERT_TRUE(sound::clipPathFor("a/b").empty());
  TEST_ASSERT_TRUE(sound::clipPathFor("..").empty());
  TEST_ASSERT_TRUE(sound::clipPathFor("../etc").empty());
  TEST_ASSERT_TRUE(sound::clipPathFor("ding.mp3").empty());
  TEST_ASSERT_TRUE(sound::clipPathFor("ding dong").empty());
  TEST_ASSERT_TRUE(sound::clipPathFor("d\\ing").empty());
  TEST_ASSERT_TRUE(sound::clipPathFor("t\xc3\xb6n").empty());
}

void test_length_cap() {
  const std::string atCap(sound::kMaxClipName, 'a');
  TEST_ASSERT_EQUAL_STRING(("/CLIPS/" + atCap + ".mp3").c_str(),
                           sound::clipPathFor(atCap).c_str());
  TEST_ASSERT_TRUE(sound::clipPathFor(atCap + "a").empty());
}

void test_name_round_trips_through_the_path() {
  TEST_ASSERT_EQUAL_STRING("ding", sound::clipNameFor(sound::clipPathFor("ding")).c_str());
  TEST_ASSERT_EQUAL_STRING("Alarm_2", sound::clipNameFor("/CLIPS/Alarm_2.mp3").c_str());
  TEST_ASSERT_TRUE(sound::clipNameFor("").empty());
  TEST_ASSERT_TRUE(sound::clipNameFor("/CLIPS/.mp3").empty());
  TEST_ASSERT_TRUE(sound::clipNameFor("/MELODIES/ding.mp3").empty());
  TEST_ASSERT_TRUE(sound::clipNameFor("/CLIPS/ding.txt").empty());
  TEST_ASSERT_TRUE(sound::clipNameFor("ding.mp3").empty());
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_plain_names_map_into_sounds);
  RUN_TEST(test_unusable_names_yield_empty);
  RUN_TEST(test_length_cap);
  RUN_TEST(test_name_round_trips_through_the_path);
  UNITY_END();
  return 0;
}
