#include <unity.h>

#include <string>

#include "core/sound/SoundMp3.h"

using namespace awtrix;

namespace {

void test_plain_names_map_into_sounds() {
  TEST_ASSERT_EQUAL_STRING("/MP3/ding.mp3", sound::mp3PathFor("ding").c_str());
  TEST_ASSERT_EQUAL_STRING("/MP3/Alarm_2.mp3", sound::mp3PathFor("Alarm_2").c_str());
  TEST_ASSERT_EQUAL_STRING("/MP3/a-b.mp3", sound::mp3PathFor("a-b").c_str());
  TEST_ASSERT_EQUAL_STRING("/MP3/7.mp3", sound::mp3PathFor("7").c_str());
}

void test_unusable_names_yield_empty() {
  TEST_ASSERT_TRUE(sound::mp3PathFor("").empty());
  TEST_ASSERT_TRUE(sound::mp3PathFor("a/b").empty());
  TEST_ASSERT_TRUE(sound::mp3PathFor("..").empty());
  TEST_ASSERT_TRUE(sound::mp3PathFor("../etc").empty());
  TEST_ASSERT_TRUE(sound::mp3PathFor("ding.mp3").empty());
  TEST_ASSERT_TRUE(sound::mp3PathFor("ding dong").empty());
  TEST_ASSERT_TRUE(sound::mp3PathFor("d\\ing").empty());
  TEST_ASSERT_TRUE(sound::mp3PathFor("t\xc3\xb6n").empty());
}

void test_length_cap() {
  const std::string atCap(sound::kMaxMp3Name, 'a');
  TEST_ASSERT_EQUAL_STRING(("/MP3/" + atCap + ".mp3").c_str(),
                           sound::mp3PathFor(atCap).c_str());
  TEST_ASSERT_TRUE(sound::mp3PathFor(atCap + "a").empty());
}

void test_name_round_trips_through_the_path() {
  TEST_ASSERT_EQUAL_STRING("ding", sound::mp3NameFor(sound::mp3PathFor("ding")).c_str());
  TEST_ASSERT_EQUAL_STRING("Alarm_2", sound::mp3NameFor("/MP3/Alarm_2.mp3").c_str());
  TEST_ASSERT_TRUE(sound::mp3NameFor("").empty());
  TEST_ASSERT_TRUE(sound::mp3NameFor("/MP3/.mp3").empty());
  TEST_ASSERT_TRUE(sound::mp3NameFor("/MELODIES/ding.mp3").empty());
  TEST_ASSERT_TRUE(sound::mp3NameFor("/MP3/ding.txt").empty());
  TEST_ASSERT_TRUE(sound::mp3NameFor("ding.mp3").empty());
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
