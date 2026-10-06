#include <unity.h>

#include "core/RuntimeState.h"
#include "core/Settings.h"
#include "core/sensing/BrightnessPolicy.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

namespace {

struct Rig {
  Settings settings;
  RuntimeState runtime;
  LightConfig light;
  BrightnessPolicy policy;
  int64_t now = 0;

  uint8_t frame(int64_t stepMs = 25) {
    now += stepMs;
    return policy.resolve(settings, runtime, light, now);
  }
};

void test_manual_brightness_is_shown_as_set() {
  Rig r;
  r.settings.brightness = 37;
  TEST_ASSERT_EQUAL_UINT8(37, r.frame());
  r.settings.brightness = 300;
  TEST_ASSERT_EQUAL_UINT8(255, r.frame());
  r.settings.brightness = -4;
  TEST_ASSERT_EQUAL_UINT8(0, r.frame());
}

void test_auto_brightness_without_a_sensor_keeps_the_manual_level() {
  Rig r;
  r.settings.brightness = 90;
  r.settings.autoBrightness = true;
  r.runtime.hasLightSensor = false;
  r.runtime.lightLevel = 100.0f;
  TEST_ASSERT_EQUAL_UINT8(90, r.frame());
}

void test_auto_brightness_follows_the_sensor() {
  Rig r;
  r.settings.brightness = 90;
  r.settings.autoBrightness = true;
  r.runtime.hasLightSensor = true;
  r.runtime.lightLevel = 100.0f;
  TEST_ASSERT_EQUAL_UINT8(brightnessFromLightLevel(100.0f, r.light), r.frame());
  r.runtime.lightLevel = 0.0f;
  TEST_ASSERT_EQUAL_UINT8(brightnessFromLightLevel(0.0f, r.light), r.frame());
}

void test_switching_to_auto_starts_at_the_sensor_level_instead_of_fading() {
  Rig r;
  r.light.smoothingMs = 10000;
  r.runtime.hasLightSensor = true;
  r.runtime.lightLevel = 100.0f;
  r.settings.brightness = 5;
  TEST_ASSERT_EQUAL_UINT8(5, r.frame());
  r.settings.autoBrightness = true;
  TEST_ASSERT_EQUAL_UINT8(r.light.maxBrightness, r.frame());
}

void test_smoothing_follows_real_elapsed_time() {
  Rig a, b;
  for (Rig* r : {&a, &b}) {
    r->light.smoothingMs = 1000;
    r->runtime.hasLightSensor = true;
    r->settings.autoBrightness = true;
    r->runtime.lightLevel = 0.0f;
    r->frame();
    r->runtime.lightLevel = 100.0f;
  }
  uint8_t fine = 0;
  for (int i = 0; i < 40; ++i) fine = a.frame(25);
  const uint8_t coarse = b.frame(1000);
  TEST_ASSERT_UINT8_WITHIN(2, coarse, fine);
  TEST_ASSERT_TRUE(coarse > a.light.minBrightness);
  TEST_ASSERT_TRUE(coarse < a.light.maxBrightness);
}

void test_a_repeated_timestamp_keeps_the_smoothed_level() {
  Rig r;
  r.light.smoothingMs = 1000;
  r.runtime.hasLightSensor = true;
  r.settings.autoBrightness = true;
  r.runtime.lightLevel = 0.0f;
  r.frame();
  r.runtime.lightLevel = 100.0f;
  const uint8_t level = r.frame(200);
  TEST_ASSERT_EQUAL_UINT8(level, r.frame(0));
}

void test_moodlight_overrides_both_modes() {
  Rig r;
  r.settings.brightness = 200;
  r.runtime.moodlightMode = true;
  r.runtime.moodlightBrightness = 23;
  TEST_ASSERT_EQUAL_UINT8(23, r.frame());
  r.settings.autoBrightness = true;
  r.runtime.hasLightSensor = true;
  TEST_ASSERT_EQUAL_UINT8(23, r.frame());
  r.runtime.moodlightMode = false;
  r.settings.autoBrightness = false;
  TEST_ASSERT_EQUAL_UINT8(200, r.frame());
}

void test_leaving_a_moodlight_resumes_the_sensor_level_it_kept_following() {
  Rig r;
  r.light.smoothingMs = 1000;
  r.runtime.hasLightSensor = true;
  r.settings.autoBrightness = true;
  r.runtime.lightLevel = 0.0f;
  r.frame();
  r.runtime.moodlightMode = true;
  r.runtime.lightLevel = 100.0f;
  for (int i = 0; i < 400; ++i) r.frame(25);
  r.runtime.moodlightMode = false;
  TEST_ASSERT_EQUAL_UINT8(r.light.maxBrightness, r.frame());
}

}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_manual_brightness_is_shown_as_set);
  RUN_TEST(test_auto_brightness_without_a_sensor_keeps_the_manual_level);
  RUN_TEST(test_auto_brightness_follows_the_sensor);
  RUN_TEST(test_switching_to_auto_starts_at_the_sensor_level_instead_of_fading);
  RUN_TEST(test_smoothing_follows_real_elapsed_time);
  RUN_TEST(test_a_repeated_timestamp_keeps_the_smoothed_level);
  RUN_TEST(test_moodlight_overrides_both_modes);
  RUN_TEST(test_leaving_a_moodlight_resumes_the_sensor_level_it_kept_following);
  return UNITY_END();
}
