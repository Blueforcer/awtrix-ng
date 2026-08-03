#include <unity.h>

#include <initializer_list>

#include "core/sensing/AutoBrightness.h"

using namespace awtrix;

namespace {

LightConfig defaults() { return LightConfig{}; }


void test_light_level_is_linear_in_raw() {
  const LightConfig c = defaults();
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, lightLevelFromRaw(0, c));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, lightLevelFromRaw(2048, c));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, lightLevelFromRaw(4095, c));
}

void test_light_level_is_not_the_raw_count() {
  const LightConfig c = defaults();
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 62.5f, lightLevelFromRaw(2560, c));
  TEST_ASSERT_TRUE(lightLevelFromRaw(2560, c) <= 100.0f);
}

void test_light_level_ignores_gamma() {
  LightConfig a = defaults();
  LightConfig b = defaults();
  a.gamma = 1.0f;
  b.gamma = 3.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, lightLevelFromRaw(2560, a), lightLevelFromRaw(2560, b));
}

void test_light_level_on_ground_inverts() {
  LightConfig c = defaults();
  c.onGround = true;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, lightLevelFromRaw(0, c));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, lightLevelFromRaw(4095, c));
}

void test_light_level_factor_scales_and_clamps() {
  LightConfig c = defaults();
  c.factor = 2.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, lightLevelFromRaw(1024, c));
  c.factor = 4.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, lightLevelFromRaw(2000, c));
}

void test_light_level_factor_zero_falls_back_to_unity() {
  LightConfig c = defaults();
  c.factor = 0.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, lightLevelFromRaw(2048, c));
}


void test_brightness_at_measured_points() {
  const LightConfig c = defaults();
  TEST_ASSERT_UINT8_WITHIN(1, 18, brightnessFromLightLevel(22.6f, c));
  TEST_ASSERT_UINT8_WITHIN(1, 85, brightnessFromLightLevel(62.5f, c));
  TEST_ASSERT_EQUAL_UINT8(220, brightnessFromLightLevel(100.0f, c));
}

void test_brightness_spans_configured_range() {
  const LightConfig c = defaults();
  TEST_ASSERT_EQUAL_UINT8(10, brightnessFromLightLevel(0.0f, c));
  TEST_ASSERT_EQUAL_UINT8(220, brightnessFromLightLevel(100.0f, c));
}

void test_brightness_gamma_zero_is_linear() {
  LightConfig c = defaults();
  c.gamma = 0.0f;
  TEST_ASSERT_UINT8_WITHIN(1, 141, brightnessFromLightLevel(62.5f, c));
}

void test_brightness_curve_is_monotonic() {
  const LightConfig c = defaults();
  uint8_t prev = 0;
  for (int pct = 0; pct <= 100; ++pct) {
    const uint8_t b = brightnessFromLightLevel(static_cast<float>(pct), c);
    TEST_ASSERT_TRUE(b >= prev);
    prev = b;
  }
}

void test_brightness_clamps_out_of_range_input() {
  const LightConfig c = defaults();
  TEST_ASSERT_EQUAL_UINT8(10, brightnessFromLightLevel(-5.0f, c));
  TEST_ASSERT_EQUAL_UINT8(220, brightnessFromLightLevel(150.0f, c));
}

void test_brightness_window_is_order_independent() {
  LightConfig normal = defaults();
  normal.minBrightness = 10;
  normal.maxBrightness = 220;
  LightConfig swapped = defaults();
  swapped.minBrightness = 220;
  swapped.maxBrightness = 10;
  for (float lvl : {0.0f, 25.0f, 50.0f, 100.0f})
    TEST_ASSERT_EQUAL_UINT8(brightnessFromLightLevel(lvl, normal),
                            brightnessFromLightLevel(lvl, swapped));
  TEST_ASSERT_TRUE(brightnessFromLightLevel(100.0f, swapped) >
                   brightnessFromLightLevel(0.0f, swapped));
}


void test_smoother_first_sample_does_not_fade_in() {
  BrightnessSmoother sm;
  sm.setTimeConstant(10000);
  TEST_ASSERT_EQUAL_UINT8(200, sm.apply(200, 100));
}

void test_smoother_zero_time_constant_is_a_passthrough() {
  BrightnessSmoother sm;
  sm.setTimeConstant(0);
  sm.reset(0);
  TEST_ASSERT_EQUAL_UINT8(200, sm.apply(200, 100));
  TEST_ASSERT_EQUAL_UINT8(5, sm.apply(5, 100));
}

void test_smoother_approaches_a_step_gradually() {
  BrightnessSmoother sm;
  sm.setTimeConstant(10000);
  sm.reset(0);
  const uint8_t first = sm.apply(200, 100);
  TEST_ASSERT_TRUE(first > 0);
  TEST_ASSERT_TRUE(first < 10);
  uint8_t v = first;
  for (int i = 0; i < 99; ++i) v = sm.apply(200, 100);
  TEST_ASSERT_TRUE(v > 110);
  TEST_ASSERT_TRUE(v < 145);
}

void test_smoother_reaches_the_target() {
  BrightnessSmoother sm;
  sm.setTimeConstant(1000);
  sm.reset(0);
  uint8_t v = 0;
  for (int i = 0; i < 200; ++i) v = sm.apply(200, 100);
  TEST_ASSERT_EQUAL_UINT8(200, v);
  for (int i = 0; i < 200; ++i) v = sm.apply(0, 100);
  TEST_ASSERT_EQUAL_UINT8(0, v);
}

void test_smoother_averages_an_oscillating_source() {
  BrightnessSmoother sm;
  sm.setTimeConstant(10000);
  sm.reset(100);
  uint8_t v = 100;
  for (int i = 0; i < 600; ++i) v = sm.apply(i % 2 ? 160 : 40, 100);
  TEST_ASSERT_TRUE(v > 90);
  TEST_ASSERT_TRUE(v < 110);
}

void test_smoother_reset_adopts_immediately() {
  BrightnessSmoother sm;
  sm.setTimeConstant(10000);
  sm.reset(0);
  sm.apply(200, 100);
  sm.reset(180);
  TEST_ASSERT_EQUAL_UINT8(180, sm.apply(180, 100));
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_light_level_is_linear_in_raw);
  RUN_TEST(test_light_level_is_not_the_raw_count);
  RUN_TEST(test_light_level_ignores_gamma);
  RUN_TEST(test_light_level_on_ground_inverts);
  RUN_TEST(test_light_level_factor_scales_and_clamps);
  RUN_TEST(test_light_level_factor_zero_falls_back_to_unity);
  RUN_TEST(test_brightness_at_measured_points);
  RUN_TEST(test_brightness_spans_configured_range);
  RUN_TEST(test_brightness_gamma_zero_is_linear);
  RUN_TEST(test_brightness_curve_is_monotonic);
  RUN_TEST(test_brightness_clamps_out_of_range_input);
  RUN_TEST(test_brightness_window_is_order_independent);
  RUN_TEST(test_smoother_first_sample_does_not_fade_in);
  RUN_TEST(test_smoother_zero_time_constant_is_a_passthrough);
  RUN_TEST(test_smoother_approaches_a_step_gradually);
  RUN_TEST(test_smoother_reaches_the_target);
  RUN_TEST(test_smoother_averages_an_oscillating_source);
  RUN_TEST(test_smoother_reset_adopts_immediately);
  UNITY_END();
  return 0;
}
