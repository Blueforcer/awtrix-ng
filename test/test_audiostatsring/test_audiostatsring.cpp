#include <unity.h>

#include <cstdint>
#include <thread>

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

// The 32-bit deadline must not come back to life once the clock has moved half its range on: not
// before anyone ever asked, and not long after the last request ran out.
void test_interest_survives_the_32_bit_wrap() {
  StatsRing never;
  TEST_ASSERT_FALSE(never.wanted((1LL << 31) + 5));
  TEST_ASSERT_FALSE(never.wanted((1LL << 32) - 5));

  StatsRing once;
  once.markInterest(1000);
  TEST_ASSERT_TRUE(once.wanted(1500));
  TEST_ASSERT_FALSE(once.wanted(1000 + StatsRing::kInterestMs));
  TEST_ASSERT_FALSE(once.wanted(1000 + (1LL << 31) + 100));
  TEST_ASSERT_FALSE(once.wanted(1000 + (1LL << 32) - 100));
}

void test_stamped_values_survive_overwrite_and_parallel_publication() {
  struct Value { uint32_t index, inverse; };
  StampedRing<Value, 8> ring;
  Value value{7, 9};
  int64_t at = 42;
  for (uint32_t id = 0; id < 8; ++id) TEST_ASSERT_FALSE(ring.read(id, at, value));
  TEST_ASSERT_FALSE(ring.read(UINT32_MAX, at, value));
  TEST_ASSERT_EQUAL_INT64(42, at);
  TEST_ASSERT_EQUAL_UINT32(7, value.index);
  for (uint32_t i = 1; i <= 9; ++i) ring.publish({i, ~i}, i * 1000LL);
  TEST_ASSERT_FALSE(ring.read(1, at, value));
  TEST_ASSERT_TRUE(ring.read(ring.head(), at, value));
  TEST_ASSERT_EQUAL_INT64(9000, at);
  std::atomic<bool> finished{false};
  std::thread writer([&] {
    for (uint32_t i = 10; i < 50000; ++i) ring.publish({i, ~i}, i * 1000LL);
    finished.store(true, std::memory_order_release);
  });
  bool consistent = true;
  do {
    if (ring.read(ring.head(), at, value))
      consistent = consistent && value.inverse == ~value.index && at == value.index * 1000LL;
  } while (!finished.load(std::memory_order_acquire));
  writer.join();
  TEST_ASSERT_TRUE(consistent);
  TEST_ASSERT_TRUE(ring.read(ring.head(), at, value));
  TEST_ASSERT_EQUAL_UINT32(49999, value.index);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_stamped_values_survive_overwrite_and_parallel_publication);
  RUN_TEST(test_empty_ring_is_inactive);
  RUN_TEST(test_future_slot_waits);
  RUN_TEST(test_picks_the_newest_audible_slot);
  RUN_TEST(test_stale_slot_is_inactive);
  RUN_TEST(test_beat_fires_once_per_slot);
  RUN_TEST(test_beat_between_render_frames_is_kept);
  RUN_TEST(test_beat_in_a_not_yet_audible_slot_waits);
  RUN_TEST(test_wraps_after_eight);
  RUN_TEST(test_interest_expires);
  RUN_TEST(test_interest_survives_the_32_bit_wrap);
  return UNITY_END();
}
