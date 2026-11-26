#include <unity.h>

#include "core/remote/RemoteButtons.h"

using namespace awtrix::remote;

void setUp() {}
void tearDown() {}

static void test_a_press_counts_once_while_held() {
  RemoteButtons b;
  b.update(1, true);
  b.update(1, true);
  TEST_ASSERT_EQUAL(0b010, b.down());
  TEST_ASSERT_EQUAL_MESSAGE(1, b.presses()[1], "the first frame down is the press");
  b.update(1, false);
  TEST_ASSERT_EQUAL(0, b.down());
  b.update(1, true);
  TEST_ASSERT_EQUAL(2, b.presses()[1]);
}

static void test_buttons_are_independent() {
  RemoteButtons b;
  b.update(0, true);
  b.update(2, true);
  TEST_ASSERT_EQUAL(0b101, b.down());
  TEST_ASSERT_EQUAL(1, b.presses()[0]);
  TEST_ASSERT_EQUAL(0, b.presses()[1]);
  TEST_ASSERT_EQUAL(1, b.presses()[2]);
}

static void test_the_count_wraps_and_strangers_are_ignored() {
  RemoteButtons b;
  for (int i = 0; i < 256; ++i) {
    b.update(0, true);
    b.update(0, false);
  }
  TEST_ASSERT_EQUAL(0, b.presses()[0]);
  b.update(3, true);
  b.update(-1, true);
  TEST_ASSERT_EQUAL(0, b.down());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_press_counts_once_while_held);
  RUN_TEST(test_buttons_are_independent);
  RUN_TEST(test_the_count_wraps_and_strangers_are_ignored);
  return UNITY_END();
}
