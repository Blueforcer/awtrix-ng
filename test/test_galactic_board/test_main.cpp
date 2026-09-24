#include <unity.h>
#include "hal/GalacticUnicornDefaults.h"
#include "hal/GalacticUnicornDisplay.h"
using namespace awtrix;
void setUp() {}
void tearDown() {}
void geometry_and_defaults() {
  const auto cfg = galacticUnicornDefaults();
  TEST_ASSERT_EQUAL(53,cfg.panelWidth);
  TEST_ASSERT_EQUAL(11,cfg.panelHeight);
  TEST_ASSERT_EQUAL(1,cfg.panels);
  TEST_ASSERT_FALSE(cfg.scriptingEnabled);
  TEST_ASSERT_EQUAL(53,galactic::Width);
  TEST_ASSERT_EQUAL(8,galactic::sanitizeHeight(8));
  TEST_ASSERT_EQUAL(11,galactic::sanitizeHeight(16));
}
void input_pin_contract() {
  TEST_ASSERT_EQUAL(28,galactic::LightSensor);
  TEST_ASSERT_EQUAL(2,galactic::LightAdc);
  TEST_ASSERT_EQUAL(0,galactic::ButtonA);
  TEST_ASSERT_EQUAL(1,galactic::ButtonB);
  TEST_ASSERT_EQUAL(3,galactic::ButtonC);
  TEST_ASSERT_EQUAL(6,galactic::ButtonD);
  TEST_ASSERT_EQUAL(27,galactic::Sleep);
  TEST_ASSERT_EQUAL(7,galactic::VolumeUp);
  TEST_ASSERT_EQUAL(8,galactic::VolumeDown);
  TEST_ASSERT_EQUAL(21,galactic::BrightnessUp);
  TEST_ASSERT_EQUAL(26,galactic::BrightnessDown);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(geometry_and_defaults);
  RUN_TEST(input_pin_contract);
  return UNITY_END();
}
