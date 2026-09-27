#include <unity.h>
#include "hal/GalacticUnicornDefaults.h"
#include "hal/GalacticUnicornDisplay.h"
#include "core/ConfigRules.h"
#include "core/SocProfileJson.h"
using namespace awtrix;
void setUp() {}
void tearDown() {}
void geometry_and_defaults() {
  const auto cfg = galacticUnicornDefaults();
  TEST_ASSERT_EQUAL(53,cfg.panelWidth);
  TEST_ASSERT_EQUAL(11,cfg.panelHeight);
  TEST_ASSERT_EQUAL(1,cfg.panels);
  TEST_ASSERT_TRUE(cfg.scriptingEnabled); // the shared default: scripts run unless switched off
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
// The API takes only the geometry the board draws, which is the geometry the driver uses.
void fixed_panel_rules() {
  const auto& soc = pins::rp2040Profile();
  TEST_ASSERT_EQUAL(galactic::Width, soc.fixedPanelWidth);
  for (int h : soc.fixedPanelHeights) TEST_ASSERT_EQUAL(h, galactic::sanitizeHeight(h));
  cfgrules::ConfigError err;
  TEST_ASSERT_TRUE(cfgrules::validateFixedPanel(soc, 53, 1, 11, err));
  TEST_ASSERT_TRUE(cfgrules::validateFixedPanel(soc, 53, 1, 8, err));
  TEST_ASSERT_FALSE(cfgrules::validateFixedPanel(soc, 64, 1, 11, err));
  TEST_ASSERT_EQUAL_STRING("panelWidth", err.field.c_str());
  TEST_ASSERT_FALSE(cfgrules::validateFixedPanel(soc, 53, 2, 11, err));
  TEST_ASSERT_EQUAL_STRING("panels", err.field.c_str());
  TEST_ASSERT_FALSE(cfgrules::validateFixedPanel(soc, 53, 1, 14, err));
  TEST_ASSERT_EQUAL_STRING("panelHeight", err.field.c_str());
  TEST_ASSERT_TRUE(cfgrules::validateFixedPanel(pins::esp32Profile(), 64, 1, 14, err));
  TEST_ASSERT_TRUE(pins::toJson(soc).find(",\"panel\":{\"width\":53,\"heights\":[8,11]}}") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(pins::toJson(pins::esp32Profile()).find("\"panel\"") == std::string::npos);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(geometry_and_defaults);
  RUN_TEST(input_pin_contract);
  RUN_TEST(fixed_panel_rules);
  return UNITY_END();
}
