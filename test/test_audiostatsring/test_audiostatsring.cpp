#include <unity.h>

#include <cstdint>

#include "core/audio/AudioStatsRing.h"

using namespace awtrix::audio;

namespace {

FrameStats stats(uint8_t level, bool beat = false) {
  FrameStats s;
  s.level = level;
  s.beat = beat;
  return s;
}

}

void setUp() {}
void tearDown() {}

void test_empty_ring_is_inactive() {
  StatsRing r;
  FrameStats out;
  TEST_ASSERT_FALSE(r.latestAudibleAt(1000, out));
}

void test_future_slot_waits() {
  StatsRing r;
  FrameStats out;
  r.publish(stats(7), 100);
  TEST_ASSERT_FALSE(r.latestAudibleAt(90, out));
  TEST_ASSERT_TRUE(r.latestAudibleAt(100, out));
  TEST_ASSERT_EQUAL_UINT8(7, out.level);
}

void test_picks_the_newest_audible_slot() {
  StatsRing r;
  FrameStats out;
  r.publish(stats(1), 100);
  r.publish(stats(2), 126);
  r.publish(stats(3), 152);
  TEST_ASSERT_TRUE(r.latestAudibleAt(130, out));
  TEST_ASSERT_EQUAL_UINT8(2, out.level);
  TEST_ASSERT_TRUE(r.latestAudibleAt(152, out));
  TEST_ASSERT_EQUAL_UINT8(3, out.level);
}

void test_stale_slot_is_inactive() {
  StatsRing r;
  FrameStats out;
  r.publish(stats(3), 152);
  TEST_ASSERT_TRUE(r.latestAudibleAt(152 + StatsRing::kStaleMs, out));
  TEST_ASSERT_FALSE(r.latestAudibleAt(152 + StatsRing::kStaleMs + 1, out));
}

void test_beat_fires_once_per_slot() {
  StatsRing r;
  FrameStats out;
  r.publish(stats(1, true), 100);
  TEST_ASSERT_TRUE(r.latestAudibleAt(130, out));
  TEST_ASSERT_TRUE(out.beat);
  TEST_ASSERT_TRUE(r.latestAudibleAt(140, out));
  TEST_ASSERT_FALSE(out.beat);
}

void test_beat_between_render_frames_is_kept() {
  StatsRing r;
  FrameStats out;
  r.publish(stats(1, true), 100);
  r.publish(stats(2, false), 126);
  TEST_ASSERT_TRUE(r.latestAudibleAt(130, out));
  TEST_ASSERT_EQUAL_UINT8(2, out.level);
  TEST_ASSERT_TRUE(out.beat);
}

void test_beat_in_a_not_yet_audible_slot_waits() {
  StatsRing r;
  FrameStats out;
  r.publish(stats(1), 100);
  r.publish(stats(2), 126);
  r.publish(stats(3, true), 152);
  TEST_ASSERT_TRUE(r.latestAudibleAt(150, out));
  TEST_ASSERT_FALSE(out.beat);
  TEST_ASSERT_TRUE(r.latestAudibleAt(155, out));
  TEST_ASSERT_TRUE(out.beat);
  TEST_ASSERT_TRUE(r.latestAudibleAt(160, out));
  TEST_ASSERT_FALSE(out.beat);
}

void test_wraps_after_eight() {
  StatsRing r;
  FrameStats out;
  for (int i = 1; i <= 20; ++i) r.publish(stats(static_cast<uint8_t>(i), i == 5), i * 26);
  TEST_ASSERT_TRUE(r.latestAudibleAt(20 * 26, out));
  TEST_ASSERT_EQUAL_UINT8(20, out.level);
  TEST_ASSERT_FALSE(out.beat);
}

void test_interest_expires() {
  StatsRing r;
  TEST_ASSERT_FALSE(r.wanted(0));
  r.markInterest(1000);
  TEST_ASSERT_TRUE(r.wanted(1000 + StatsRing::kInterestMs - 1));
  TEST_ASSERT_FALSE(r.wanted(1000 + StatsRing::kInterestMs));
  const int64_t late = (1LL << 32) + 500;
  r.markInterest(late);
  TEST_ASSERT_TRUE(r.wanted(late + 100));
  TEST_ASSERT_FALSE(r.wanted(late + StatsRing::kInterestMs + 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_ring_is_inactive);
  RUN_TEST(test_future_slot_waits);
  RUN_TEST(test_picks_the_newest_audible_slot);
  RUN_TEST(test_stale_slot_is_inactive);
  RUN_TEST(test_beat_fires_once_per_slot);
  RUN_TEST(test_beat_between_render_frames_is_kept);
  RUN_TEST(test_beat_in_a_not_yet_audible_slot_waits);
  RUN_TEST(test_wraps_after_eight);
  RUN_TEST(test_interest_expires);
  return UNITY_END();
}
