#include <unity.h>

#include <cmath>

#include "core/sound/VolumeCurve.h"

using namespace awtrix::sound;

void setUp() {}
void tearDown() {}

static void test_the_curve_runs_from_minus_60_to_minus_3_db() {
  TEST_ASSERT_EQUAL_INT8(-60, awtrix_volume_db(0));
  TEST_ASSERT_EQUAL_INT8(-3, awtrix_volume_db(100));
  TEST_ASSERT_EQUAL_INT8(-3, awtrix_volume_db(255));
  for (int v = 1; v <= 100; ++v)
    TEST_ASSERT_TRUE(awtrix_volume_db(static_cast<uint8_t>(v)) >= awtrix_volume_db(static_cast<uint8_t>(v - 1)));
}

static void test_the_curve_follows_35_log10_of_40_percent_minus_60() {
  TEST_ASSERT_EQUAL_INT8(-14, awtrix_volume_db(50));
  TEST_ASSERT_EQUAL_INT8(-30, awtrix_volume_db(17));
  for (int v = 3; v <= 100; ++v) {
    const double ideal = 35.0 * std::log10(0.4 * v) - 60.0;
    TEST_ASSERT_FLOAT_WITHIN(1.0f, static_cast<float>(ideal), awtrix_volume_db(static_cast<uint8_t>(v)));
  }
}

static void test_the_gain_is_the_db_difference_in_q15() {
  TEST_ASSERT_EQUAL_INT32(0, volumeGain(0, 100));
  TEST_ASSERT_EQUAL_INT32(kVolumeUnity, volumeGain(100, 100));
  TEST_ASSERT_EQUAL_INT32(kVolumeUnity, volumeGain(80, 60));
  TEST_ASSERT_EQUAL_INT32(9235, volumeGain(50, 100));
  TEST_ASSERT_EQUAL_INT32(14637, volumeGain(50, 80));
  for (int v = 1; v < 100; ++v) {
    const double db = awtrix_volume_db(static_cast<uint8_t>(v)) - awtrix_volume_db(100);
    const int32_t gain = volumeGain(static_cast<uint8_t>(v), 100);
    TEST_ASSERT_TRUE(gain > 0 && gain <= kVolumeUnity);
    TEST_ASSERT_INT32_WITHIN(1, std::lround(kVolumeUnity * std::pow(10.0, db / 20.0)), gain);
    TEST_ASSERT_TRUE(gain >= volumeGain(static_cast<uint8_t>(v - 1), 100));
  }
}

static void test_half_the_slider_is_not_half_the_samples() {
  TEST_ASSERT_TRUE(volumeGain(50, 100) < kVolumeUnity / 2);
  TEST_ASSERT_TRUE(volumeGain(90, 100) > kVolumeUnity * 3 / 4);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_curve_runs_from_minus_60_to_minus_3_db);
  RUN_TEST(test_the_curve_follows_35_log10_of_40_percent_minus_60);
  RUN_TEST(test_the_gain_is_the_db_difference_in_q15);
  RUN_TEST(test_half_the_slider_is_not_half_the_samples);
  return UNITY_END();
}
