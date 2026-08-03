#include <unity.h>

#include <string>

#include "transport/MqttTopicMatch.h"

using awtrix::mqtt::topicMatches;

namespace {

void test_exact_match() {
  TEST_ASSERT_TRUE(topicMatches("sport/tennis/player1", "sport/tennis/player1"));
  TEST_ASSERT_FALSE(topicMatches("sport/tennis/player1", "sport/tennis/player2"));
  TEST_ASSERT_FALSE(topicMatches("sport/tennis", "sport/tennis/player1"));
  TEST_ASSERT_FALSE(topicMatches("sport/tennis/player1", "sport/tennis"));
  TEST_ASSERT_TRUE(topicMatches("a", "a"));
  TEST_ASSERT_FALSE(topicMatches("a", "b"));
}

void test_partial_level_is_not_a_match() {
  TEST_ASSERT_FALSE(topicMatches("sport/+", "sports/tennis"));
  TEST_ASSERT_FALSE(topicMatches("sport/#", "sports/tennis"));
  TEST_ASSERT_FALSE(topicMatches("sport", "sports"));
}

void test_hash_matches_remaining_levels() {
  TEST_ASSERT_TRUE(topicMatches("sport/tennis/player1/#", "sport/tennis/player1"));
  TEST_ASSERT_TRUE(topicMatches("sport/tennis/player1/#", "sport/tennis/player1/ranking"));
  TEST_ASSERT_TRUE(topicMatches("sport/tennis/player1/#", "sport/tennis/player1/score/wimbledon"));
}

void test_hash_matches_the_parent_level_itself() {
  TEST_ASSERT_TRUE(topicMatches("sport/#", "sport"));
  TEST_ASSERT_TRUE(topicMatches("sport/#", "sport/tennis"));
  TEST_ASSERT_FALSE(topicMatches("sport/#", "sporting"));
}

void test_bare_hash_matches_everything_not_dollar() {
  TEST_ASSERT_TRUE(topicMatches("#", "a"));
  TEST_ASSERT_TRUE(topicMatches("#", "a/b/c"));
  TEST_ASSERT_TRUE(topicMatches("#", "/"));
}

void test_hash_must_be_the_last_level() {
  TEST_ASSERT_FALSE(topicMatches("sport/#/ranking", "sport/tennis/ranking"));
  TEST_ASSERT_FALSE(topicMatches("#/sport", "sport"));
  TEST_ASSERT_FALSE(topicMatches("#/#", "a/b"));
}

void test_hash_is_not_a_substring_wildcard() {
  TEST_ASSERT_FALSE(topicMatches("sport#", "sport/tennis"));
  TEST_ASSERT_FALSE(topicMatches("sport#", "sportx"));
}

void test_plus_matches_exactly_one_level() {
  TEST_ASSERT_TRUE(topicMatches("sport/tennis/+", "sport/tennis/player1"));
  TEST_ASSERT_TRUE(topicMatches("sport/tennis/+", "sport/tennis/player2"));
  TEST_ASSERT_FALSE(topicMatches("sport/tennis/+", "sport/tennis/player1/ranking"));
  TEST_ASSERT_FALSE(topicMatches("sport/+", "sport"));
  TEST_ASSERT_TRUE(topicMatches("sport/+", "sport/"));
  TEST_ASSERT_TRUE(topicMatches("+/tennis/#", "sport/tennis/player1"));
}

void test_plus_is_not_a_substring_wildcard() {
  TEST_ASSERT_FALSE(topicMatches("sport+", "sport/tennis"));
  TEST_ASSERT_FALSE(topicMatches("sport+", "sportx"));
}

void test_plus_matches_empty_levels() {
  TEST_ASSERT_TRUE(topicMatches("+/+", "/finance"));
  TEST_ASSERT_TRUE(topicMatches("/+", "/finance"));
  TEST_ASSERT_FALSE(topicMatches("+", "/finance"));
  TEST_ASSERT_TRUE(topicMatches("a/+/c", "a//c"));
}

void test_wildcards_do_not_match_dollar_topics() {
  TEST_ASSERT_FALSE(topicMatches("#", "$SYS/broker/uptime"));
  TEST_ASSERT_FALSE(topicMatches("+/monitor/Clients", "$SYS/monitor/Clients"));
  TEST_ASSERT_TRUE(topicMatches("$SYS/#", "$SYS/broker/uptime"));
  TEST_ASSERT_TRUE(topicMatches("$SYS/monitor/+", "$SYS/monitor/Clients"));
  TEST_ASSERT_TRUE(topicMatches("a/#", "a/$SYS"));
}

void test_empty_inputs_never_match() {
  TEST_ASSERT_FALSE(topicMatches("", ""));
  TEST_ASSERT_FALSE(topicMatches("", "a"));
  TEST_ASSERT_FALSE(topicMatches("a", ""));
  TEST_ASSERT_FALSE(topicMatches("#", ""));
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_exact_match);
  RUN_TEST(test_partial_level_is_not_a_match);
  RUN_TEST(test_hash_matches_remaining_levels);
  RUN_TEST(test_hash_matches_the_parent_level_itself);
  RUN_TEST(test_bare_hash_matches_everything_not_dollar);
  RUN_TEST(test_hash_must_be_the_last_level);
  RUN_TEST(test_hash_is_not_a_substring_wildcard);
  RUN_TEST(test_plus_matches_exactly_one_level);
  RUN_TEST(test_plus_is_not_a_substring_wildcard);
  RUN_TEST(test_plus_matches_empty_levels);
  RUN_TEST(test_wildcards_do_not_match_dollar_topics);
  RUN_TEST(test_empty_inputs_never_match);
  UNITY_END();
  return 0;
}
