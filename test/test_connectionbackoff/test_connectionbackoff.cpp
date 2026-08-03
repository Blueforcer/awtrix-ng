
#include <unity.h>

#include "core/net/ConnectionBackoff.h"

using namespace awtrix::net;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kEntropy = 12345u;

void assertWithinJitterBand(uint32_t got, uint32_t ceiling, const char* what) {
  const uint32_t floor = ceiling - ceiling / 100u * ConnectionBackoff::kJitterPercent;
  TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(ceiling, got, what);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(floor, got, what);
}

}

static void test_a_fresh_backoff_is_due_immediately() {
  ConnectionBackoff b;
  TEST_ASSERT_TRUE(b.due(0));
  TEST_ASSERT_TRUE(b.due(999999));
  TEST_ASSERT_EQUAL_UINT32(0, b.retryInMs(0));
  TEST_ASSERT_EQUAL_UINT16(0, b.attempts());
}

static void test_delay_doubles_from_five_seconds_to_a_sixty_second_ceiling() {
  ConnectionBackoff b;
  const uint32_t expected[] = {5000, 10000, 20000, 40000, 60000, 60000, 60000};
  for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    assertWithinJitterBand(b.onFailure(kEntropy + i), expected[i], "backoff step");
}

static void test_the_ceiling_holds_under_many_failures() {
  ConnectionBackoff b;
  for (int i = 0; i < 50; ++i) {
    const uint32_t d = b.onFailure(kEntropy + static_cast<uint32_t>(i));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(ConnectionBackoff::kMaxRetryMs, d);
  }
  assertWithinJitterBand(b.delayMs(), ConnectionBackoff::kMaxRetryMs, "saturated");
}

static void test_success_collapses_the_delay_and_the_attempt_count() {
  ConnectionBackoff b;
  for (int i = 0; i < 5; ++i) b.onFailure(kEntropy);
  TEST_ASSERT_EQUAL_UINT16(5, b.attempts());

  b.onSuccess();
  TEST_ASSERT_EQUAL_UINT32(ConnectionBackoff::kFirstRetryMs, b.delayMs());
  TEST_ASSERT_EQUAL_UINT16(0, b.attempts());
  assertWithinJitterBand(b.onFailure(kEntropy), 5000, "after success");
}

static void test_due_respects_the_armed_delay() {
  ConnectionBackoff b;
  b.onFailure(0);
  const uint32_t d = b.delayMs();
  b.arm(1000);

  TEST_ASSERT_FALSE(b.due(1000));
  TEST_ASSERT_FALSE(b.due(1000 + d - 1));
  TEST_ASSERT_TRUE(b.due(1000 + d));
  TEST_ASSERT_TRUE(b.due(1000 + d + 5000));

  TEST_ASSERT_EQUAL_UINT32(d, b.retryInMs(1000));
  TEST_ASSERT_EQUAL_UINT32(1, b.retryInMs(1000 + d - 1));
  TEST_ASSERT_EQUAL_UINT32(0, b.retryInMs(1000 + d));
}

static void test_due_survives_the_millis_wrap() {
  ConnectionBackoff b;
  b.onFailure(0);
  const uint32_t d = b.delayMs();
  const uint32_t armedAt = 0xFFFFFFFFu - (d / 2);
  b.arm(armedAt);

  TEST_ASSERT_FALSE(b.due(armedAt));
  TEST_ASSERT_FALSE(b.due(static_cast<uint32_t>(armedAt + d - 1)));
  TEST_ASSERT_TRUE(b.due(static_cast<uint32_t>(armedAt + d)));
}

static void test_attempts_saturate_instead_of_wrapping() {
  ConnectionBackoff b;
  for (uint32_t i = 0; i < 70000u; ++i) b.onFailure(kEntropy);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, b.attempts());
}

static void test_jitter_varies_with_entropy_but_stays_in_band() {
  bool sawTwoValues = false;
  uint32_t first = 0;
  for (uint32_t e = 1; e <= 40; ++e) {
    ConnectionBackoff b;
    for (int i = 0; i < 6; ++i) b.onFailure(e * 7919u + static_cast<uint32_t>(i));
    assertWithinJitterBand(b.delayMs(), ConnectionBackoff::kMaxRetryMs, "jittered ceiling");
    if (e == 1) first = b.delayMs();
    else if (b.delayMs() != first) sawTwoValues = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(sawTwoValues, "jitter must actually spread the retries");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_fresh_backoff_is_due_immediately);
  RUN_TEST(test_delay_doubles_from_five_seconds_to_a_sixty_second_ceiling);
  RUN_TEST(test_the_ceiling_holds_under_many_failures);
  RUN_TEST(test_success_collapses_the_delay_and_the_attempt_count);
  RUN_TEST(test_due_respects_the_armed_delay);
  RUN_TEST(test_due_survives_the_millis_wrap);
  RUN_TEST(test_attempts_saturate_instead_of_wrapping);
  RUN_TEST(test_jitter_varies_with_entropy_but_stays_in_band);
  return UNITY_END();
}
