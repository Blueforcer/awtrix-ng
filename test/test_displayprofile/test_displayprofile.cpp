#include <unity.h>

#include "core/PlatformDescriptor.h"
#include "core/api/CapabilitiesJson.h"
#include "platform/linux/LinuxCapabilities.h"
#include "core/api/JsonReader.h"
#include "core/api/StateJson.h"
#include "core/apps/SpecRenderer.h"
#include "core/payload/PayloadParser.h"
#include "core/render/MatrixLayout.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void test_logical_geometry_does_not_change_esp_wiring() {
  const DisplayProfile native{52, 16, false};
  TEST_ASSERT_TRUE(native.valid());
  TEST_ASSERT_EQUAL_UINT(832, native.pixelCount());
  const DisplayProfile invalid{0, 16, false};
  TEST_ASSERT_FALSE(invalid.valid());
  TEST_ASSERT_EQUAL_UINT(0, invalid.pixelCount());
  for (int width : {32, 64, 128}) {
    MatrixLayout matrix;
    matrix.panelWidth = width;
    matrix = sanitizeMatrixLayout(matrix);
    TEST_ASSERT_EQUAL_INT(width, matrix.width());
    TEST_ASSERT_EQUAL_INT(8, matrix.height());
    TEST_ASSERT_EQUAL_INT(width * 8, matrix.ledCount());
  }
}

static void test_native_canvas_and_screen_keep_every_physical_pixel() {
  const DisplayProfile display{52, 16, false};
  Canvas c(display.width, display.height);
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x)
      c.setPixel(x, y, 1 + y * c.width() + x);
  c.setPixel(52, 0, 0xFFFFFF);
  c.setPixel(0, 16, 0xFFFFFF);
  TEST_ASSERT_EQUAL_HEX32(832, c.getPixel(51, 15));
  const std::string json = buildScreenJson(c);
  TEST_ASSERT_TRUE(api::isWellFormed(json));
  long long width = 0, height = 0;
  TEST_ASSERT_TRUE(api::memberValue(api::JsonReader(json), "width").asLong(width));
  TEST_ASSERT_TRUE(api::memberValue(api::JsonReader(json), "height").asLong(height));
  TEST_ASSERT_EQUAL_INT(52, width);
  TEST_ASSERT_EQUAL_INT(16, height);
  auto pixels = api::memberValue(api::JsonReader(json), "pixels");
  TEST_ASSERT_TRUE(pixels.enterArray());
  int count = 0;
  while (pixels.nextElement()) {
    long long color = 0;
    TEST_ASSERT_TRUE(pixels.asLong(color));
    TEST_ASSERT_EQUAL_INT(++count, color);
    TEST_ASSERT_TRUE(pixels.skipValue());
  }
  TEST_ASSERT_EQUAL_INT(832, count);
}

static void test_payload_coordinates_are_unscaled_on_native_display() {
  static const FontGlyph glyphs[] = {{0, 1, 1, 1, 0, 0}};
  static const uint8_t bitmap[] = {0x80};
  static const GfxFont font{bitmap, glyphs, 'A', 'A', 8};
  AppSpec spec;
  TEST_ASSERT_TRUE(payload::parse(
      R"({"draw":[["pixel",51,15,"#AABBCC"],["pixel",2,3,"#112233"]]})", false, spec));
  Canvas c(52, 16);
  render::renderSpec(c, spec, font, {});
  TEST_ASSERT_EQUAL_HEX32(0xAABBCC, c.getPixel(51, 15));
  TEST_ASSERT_EQUAL_HEX32(0x112233, c.getPixel(2, 3));
  int lit = 0;
  for (std::size_t i = 0; i < c.size(); ++i) lit += c.data()[i] != 0;
  TEST_ASSERT_EQUAL_INT(2, lit);
}

static void test_legacy_capabilities_keep_gpio_and_no_descriptor() {
  const std::string json = api::capabilitiesJson({}, {}, {}, {true, false, true, false});
  TEST_ASSERT_TRUE(api::isWellFormed(json));
  TEST_ASSERT_FALSE(api::present(api::memberValue(api::JsonReader(json), "platform")));
  TEST_ASSERT_FALSE(api::present(api::memberValue(api::JsonReader(json), "display")));
  const auto gpio = api::memberValue(api::JsonReader(json), "gpio");
  TEST_ASSERT_TRUE(gpio.isObject());
  TEST_ASSERT_EQUAL_STRING(pins::toJson(pins::activeProfile()).c_str(),
                           std::string(gpio.valueText()).c_str());
}

static void test_injected_capabilities_describe_fixed_display_without_esp_pins() {
  const PlatformDescriptor platform{"linux", {52, 16, false}, false};
  const std::string json = api::capabilitiesJson({}, {}, {}, {}, &platform);
  TEST_ASSERT_TRUE(api::isWellFormed(json));
  TEST_ASSERT_TRUE(api::memberValue(api::JsonReader(json), "gpio").isNull());
  const auto p = api::memberValue(api::JsonReader(json), "platform");
  TEST_ASSERT_TRUE(api::memberValue(p, "id").rawString() == "linux");
  const auto d = api::memberValue(api::JsonReader(json), "display");
  long long width = 0, height = 0;
  bool configurable = true;
  TEST_ASSERT_TRUE(api::memberValue(d, "width").asLong(width));
  TEST_ASSERT_TRUE(api::memberValue(d, "height").asLong(height));
  TEST_ASSERT_TRUE(api::memberValue(d, "configurable").asBool(configurable));
  TEST_ASSERT_EQUAL_INT(52, width);
  TEST_ASSERT_EQUAL_INT(16, height);
  TEST_ASSERT_FALSE(configurable);
}

static void test_capabilities_state_whether_a_light_sensor_exists() {
  PlatformDescriptor platform{"tc002", {52, 16, false}, false};
  bool light = true;
  std::string json = api::capabilitiesJson({}, {}, {}, {}, &platform);
  TEST_ASSERT_TRUE(api::memberValue(api::memberValue(api::JsonReader(json), "sensors"), "light")
                       .asBool(light));
  TEST_ASSERT_FALSE(light);
  platform.lightSensor = true;
  json = api::capabilitiesJson({}, {}, {}, {}, &platform);
  TEST_ASSERT_TRUE(api::memberValue(api::memberValue(api::JsonReader(json), "sensors"), "light")
                       .asBool(light));
  TEST_ASSERT_TRUE(light);
}

static void test_platform_identifier_is_json_escaped() {
  const PlatformDescriptor platform{"host\"test\n", {52, 16, false}, false};
  const std::string json = api::capabilitiesJson({}, {}, {}, {}, &platform);
  TEST_ASSERT_TRUE(api::isWellFormed(json));
  std::string id;
  TEST_ASSERT_TRUE(api::memberValue(api::memberValue(api::JsonReader(json), "platform"), "id")
                       .appendString(id));
  TEST_ASSERT_EQUAL_STRING(platform.id, id.c_str());
}

static void test_capabilities_list_clock_faces_only_where_the_platform_draws_them() {
  PlatformDescriptor platform{"tc002", {52, 16, false}, false};
  std::string json = api::capabilitiesJson({}, {}, {}, {}, &platform);
  TEST_ASSERT_FALSE(api::present(api::memberValue(api::JsonReader(json), "clockFaces")));
  LinuxCapabilities platformCaps(false, false);
  platformCaps.clockFaces = true;
  json = api::capabilitiesJson({}, {}, {}, {}, &platform, nullptr, [&](api::JsonWriter& writer) {
    platformCaps.write(writer);
  });
  TEST_ASSERT_TRUE(api::isWellFormed(json));
  TEST_ASSERT_EQUAL_STRING("[\"sheet\",\"ring\",\"flap\",\"month\",\"big\"]",
      std::string(api::memberValue(api::JsonReader(json), "clockFaces").valueText()).c_str());
}

static void test_capabilities_offer_enlarged_apps_only_where_pages_can_be_enlarged() {
  PlatformDescriptor platform{"tc002", {52, 16, false}, false};
  for (bool enlarge : {false, true}) {
    LinuxCapabilities platformCaps(false, false);
    platformCaps.enlargeApps = enlarge;
    const auto json = api::capabilitiesJson({}, {}, {}, {}, &platform, nullptr,
        [&](api::JsonWriter& writer) { platformCaps.write(writer); });
    bool advertised = false;
    TEST_ASSERT_EQUAL(enlarge, api::memberValue(api::JsonReader(json), "enlargeApps").asBool(advertised));
    TEST_ASSERT_EQUAL(enlarge, advertised);
  }
}

static void test_capabilities_separate_pending_width_and_the_esp_envelope() {
  PlatformDescriptor platform{"esp32s3", {32, 8, true}};
  platform.display.limits = kEspDisplayLimits;
  platform.display.requestedWidth = 128;
  platform.display.estimatedWireTimeUs = 7680;
  const auto json = api::capabilitiesJson({}, {}, {}, {}, &platform);
  const auto display = api::memberValue(api::JsonReader(json), "display");
  long long value = 0;
  bool pending = false;
  TEST_ASSERT_TRUE(api::memberValue(display, "width").asLong(value));
  TEST_ASSERT_EQUAL_INT(32, value);
  TEST_ASSERT_TRUE(api::memberValue(display, "requestedWidth").asLong(value));
  TEST_ASSERT_EQUAL_INT(128, value);
  TEST_ASSERT_TRUE(api::memberValue(display, "restartRequired").asBool(pending));
  TEST_ASSERT_TRUE(pending);
  TEST_ASSERT_TRUE(api::memberValue(display, "maxHeight").asLong(value));
  TEST_ASSERT_EQUAL_INT(8, value);
  TEST_ASSERT_TRUE(api::memberValue(display, "maxPixels").asLong(value));
  TEST_ASSERT_EQUAL_INT(1024, value);
  TEST_ASSERT_TRUE(api::memberValue(display, "estimatedWireTimeUs").asLong(value));
  TEST_ASSERT_EQUAL_INT(7680, value);
}

static void test_capabilities_leave_out_a_wire_time_nobody_estimated() {
  PlatformDescriptor platform{"tc002", {52, 16, false}, false};
  const auto json = api::capabilitiesJson({}, {}, {}, {}, &platform);
  TEST_ASSERT_TRUE(api::isWellFormed(json));
  const auto display = api::memberValue(api::JsonReader(json), "display");
  TEST_ASSERT_TRUE(api::present(api::memberValue(display, "maxPixels")));
  TEST_ASSERT_FALSE(api::present(api::memberValue(display, "estimatedWireTimeUs")));
  TEST_ASSERT_FALSE(api::present(api::memberValue(display, "wireTimeIsEstimate")));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_logical_geometry_does_not_change_esp_wiring);
  RUN_TEST(test_native_canvas_and_screen_keep_every_physical_pixel);
  RUN_TEST(test_payload_coordinates_are_unscaled_on_native_display);
  RUN_TEST(test_legacy_capabilities_keep_gpio_and_no_descriptor);
  RUN_TEST(test_injected_capabilities_describe_fixed_display_without_esp_pins);
  RUN_TEST(test_capabilities_state_whether_a_light_sensor_exists);
  RUN_TEST(test_platform_identifier_is_json_escaped);
  RUN_TEST(test_capabilities_list_clock_faces_only_where_the_platform_draws_them);
  RUN_TEST(test_capabilities_offer_enlarged_apps_only_where_pages_can_be_enlarged);
  RUN_TEST(test_capabilities_separate_pending_width_and_the_esp_envelope);
  RUN_TEST(test_capabilities_leave_out_a_wire_time_nobody_estimated);
  return UNITY_END();
}
