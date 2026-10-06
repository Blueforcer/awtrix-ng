#include <unity.h>
#include "core/render/FrameDiagnostics.h"

using namespace awtrix::render;
void setUp() {}
void tearDown() {}

static void test_rate_counts_completed_presentations_over_exact_elapsed_time() {
  FrameDiagnostics diagnostics;
  diagnostics.presented(1000);
  TEST_ASSERT_EQUAL_UINT32(0, diagnostics.presentationFpsMilli());
  diagnostics.presented(41000);
  diagnostics.presented(101000);
  TEST_ASSERT_EQUAL_UINT32(20000, diagnostics.presentationFpsMilli());
  diagnostics.rendered(1000);
  diagnostics.rendered(26000);
  TEST_ASSERT_EQUAL_UINT32(40000, diagnostics.renderFpsMilli());
}

static void test_percentiles_cover_the_bounded_recent_window() {
  TimingSamples timings;
  for (unsigned value = 1; value <= 100; ++value) timings.add(value);
  const auto stats = timings.snapshot();
  TEST_ASSERT_EQUAL_UINT32(100, stats.count);
  TEST_ASSERT_EQUAL_UINT32(50, stats.meanUs);
  TEST_ASSERT_EQUAL_UINT32(68, stats.p50Us);
  TEST_ASSERT_EQUAL_UINT32(97, stats.p95Us);
  TEST_ASSERT_EQUAL_UINT32(100, stats.p99Us);
  TEST_ASSERT_EQUAL_UINT32(100, stats.maxUs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rate_counts_completed_presentations_over_exact_elapsed_time);
  RUN_TEST(test_percentiles_cover_the_bounded_recent_window);
  return UNITY_END();
}
