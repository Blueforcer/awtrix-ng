#include <unity.h>

#include "core/apps/SensorFormat.h"
#include "core/render/TextEncoding.h"
#include "media/AwtrixFontAdapter.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void test_temperature_celsius() {
  TEST_ASSERT_EQUAL_STRING("23\xC2\xB0" "C", formatTemperature(23.0f, true).c_str());
}

static void test_temperature_fahrenheit() {
  TEST_ASSERT_EQUAL_STRING("68\xC2\xB0" "F", formatTemperature(20.0f, false).c_str());
}

static void test_temperature_rounding() {
  TEST_ASSERT_EQUAL_STRING("24\xC2\xB0" "C", formatTemperature(23.6f, true).c_str());
}

static void test_every_formatted_string_is_drawable() {
  const GfxFont& font = awtrixFont();
  const FontGlyph* placeholder = text::glyphFor(font, '?');
  const std::string samples[] = {
      formatTemperature(23.0f, true),  formatTemperature(20.0f, false),
      formatTemperature(-3.4f, true),  formatTemperature(21.5f, true, 1),
      formatHumidity(45.0f),           formatBattery(87),
  };
  for (const std::string& s : samples) {
    TEST_ASSERT_TRUE_MESSAGE(text::isValidUtf8(s), "formatted string is not UTF-8");
    text::GlyphIter it(font, s);
    const FontGlyph* g = nullptr;
    while (it.next(g)) {
      TEST_ASSERT_NOT_NULL(g);
      TEST_ASSERT_TRUE_MESSAGE(g != placeholder, "formatted string draws a '?'");
    }
  }
}

static void test_humidity() {
  TEST_ASSERT_EQUAL_STRING("45%", formatHumidity(45.0f).c_str());
  TEST_ASSERT_EQUAL_STRING("46%", formatHumidity(45.7f).c_str());
}

static void test_battery_clamps() {
  TEST_ASSERT_EQUAL_STRING("87%", formatBattery(87).c_str());
  TEST_ASSERT_EQUAL_STRING("100%", formatBattery(150).c_str());
  TEST_ASSERT_EQUAL_STRING("0%", formatBattery(-5).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_temperature_celsius);
  RUN_TEST(test_temperature_fahrenheit);
  RUN_TEST(test_temperature_rounding);
  RUN_TEST(test_every_formatted_string_is_drawable);
  RUN_TEST(test_humidity);
  RUN_TEST(test_battery_clamps);
  return UNITY_END();
}
