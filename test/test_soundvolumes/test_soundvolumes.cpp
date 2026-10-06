#include <unity.h>

#include "core/sound/Sound.h"

using namespace awtrix::sound;

void setUp() {}
void tearDown() {}

// Master and group multiply, rounded to the nearest percent.
static void test_heard_level_is_master_times_group() {
  TEST_ASSERT_EQUAL_UINT8(100, heard(100, 100));
  TEST_ASSERT_EQUAL_UINT8(25, heard(50, 50));
  TEST_ASSERT_EQUAL_UINT8(1, heard(10, 5));
  TEST_ASSERT_EQUAL_UINT8(0, heard(0, 100));
  TEST_ASSERT_EQUAL_UINT8(0, heard(100, 0));
}

static void test_heard_level_clamps_out_of_range_input() {
  TEST_ASSERT_EQUAL_UINT8(100, heard(150, 120));
  TEST_ASSERT_EQUAL_UINT8(0, heard(-5, 80));
}

static void test_volumes_for_maps_each_group() {
  const Volumes v = volumesFor(80, 50, 100, 25);
  TEST_ASSERT_EQUAL_UINT8(40, v.radio);
  TEST_ASSERT_EQUAL_UINT8(80, v.app);
  TEST_ASSERT_EQUAL_UINT8(20, v.alert);
  TEST_ASSERT_EQUAL_UINT8(20, v.of(Group::Alert));
  TEST_ASSERT_EQUAL_UINT8(80, v.of(Group::App));
  TEST_ASSERT_EQUAL_UINT8(40, v.of(Group::Radio));
}

static void test_volumes_compare_by_value() {
  TEST_ASSERT_TRUE(volumesFor(60, 80, 100, 100) == volumesFor(60, 80, 100, 100));
  TEST_ASSERT_TRUE(volumesFor(60, 80, 100, 100) != volumesFor(61, 80, 100, 100));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_heard_level_is_master_times_group);
  RUN_TEST(test_heard_level_clamps_out_of_range_input);
  RUN_TEST(test_volumes_for_maps_each_group);
  RUN_TEST(test_volumes_compare_by_value);
  return UNITY_END();
}
