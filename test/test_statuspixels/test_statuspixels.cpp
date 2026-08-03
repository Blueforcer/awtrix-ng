#include <unity.h>

#include "core/net/LinkStatus.h"
#include "core/render/StatusPixels.h"

using namespace awtrix;
using namespace awtrix::net;
using awtrix::render::drawLinkStatus;
using awtrix::render::pulse;

void setUp() {}
void tearDown() {}

namespace {

// Peak brightness so a lit pixel is compared against a known value rather than "not black".
constexpr int64_t kPeakMs = 1000;

LinkStatus up() {
  LinkStatus s;
  s.enabled = true;
  s.phase = LinkPhase::Connected;
  return s;
}

LinkStatus down(LinkError why) {
  LinkStatus s;
  s.enabled = true;
  s.phase = LinkPhase::Offline;
  s.error = why;
  return s;
}

LinkStatus off() { return LinkStatus{}; }

}

static void test_a_healthy_device_draws_nothing() {
  Canvas c(32, 8);
  c.clear();
  drawLinkStatus(c, up(), up(), kPeakMs);
  for (std::size_t i = 0; i < c.size(); ++i) TEST_ASSERT_EQUAL_UINT32(0u, c.data()[i]);
}

static void test_a_dropped_wifi_link_lights_the_top_left_pixel_red() {
  Canvas c(32, 8);
  c.clear();
  drawLinkStatus(c, down(LinkError::Lost), up(), kPeakMs);
  TEST_ASSERT_EQUAL_UINT32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_UINT32(0u, c.getPixel(0, 7));
}

static void test_a_dropped_broker_lights_the_bottom_left_pixel_yellow() {
  Canvas c(32, 8);
  c.clear();
  drawLinkStatus(c, up(), down(LinkError::Lost), kPeakMs);
  TEST_ASSERT_EQUAL_UINT32(0xFFFF00u, c.getPixel(0, 7));
  TEST_ASSERT_EQUAL_UINT32(0u, c.getPixel(0, 0));
}

// One dot lit has to mean one thing to go and fix. MQTT is down because the Wi-Fi is, which is
// the same outage the red pixel already reports.
static void test_mqtt_stays_dark_when_it_is_only_down_for_want_of_wifi() {
  Canvas c(32, 8);
  c.clear();
  drawLinkStatus(c, down(LinkError::Lost), down(LinkError::NoWifi), kPeakMs);
  TEST_ASSERT_EQUAL_UINT32(0xFF0000u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_UINT32(0u, c.getPixel(0, 7));
}

static void test_an_unconfigured_link_is_not_a_fault() {
  Canvas c(32, 8);
  c.clear();
  drawLinkStatus(c, off(), off(), kPeakMs);
  TEST_ASSERT_EQUAL_UINT32(0u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_UINT32(0u, c.getPixel(0, 7));
}

static void test_a_link_still_dialling_in_shows_the_dot() {
  Canvas c(32, 8);
  c.clear();
  LinkStatus joining = up();
  joining.phase = LinkPhase::Connecting;
  drawLinkStatus(c, joining, up(), kPeakMs);
  TEST_ASSERT_EQUAL_UINT32(0xFF0000u, c.getPixel(0, 0));
}

static void test_the_mqtt_pixel_follows_the_matrix_height() {
  Canvas c(64, 16);
  c.clear();
  drawLinkStatus(c, up(), down(LinkError::Lost), kPeakMs);
  TEST_ASSERT_EQUAL_UINT32(0xFFFF00u, c.getPixel(0, 15));
  TEST_ASSERT_EQUAL_UINT32(0u, c.getPixel(0, 7));
}

static void test_the_dots_pulse_rather_than_sit_at_full_brightness() {
  Canvas c(32, 8);
  c.clear();
  drawLinkStatus(c, down(LinkError::Lost), up(), 0);
  TEST_ASSERT_EQUAL_UINT32(0x000000u, c.getPixel(0, 0));
  c.clear();
  drawLinkStatus(c, down(LinkError::Lost), up(), 500);
  TEST_ASSERT_EQUAL_UINT32(0x7F0000u, c.getPixel(0, 0));
}

static void test_a_zero_period_leaves_the_colour_alone() {
  TEST_ASSERT_EQUAL_UINT32(0x123456u, pulse(0x123456u, 12345, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_healthy_device_draws_nothing);
  RUN_TEST(test_a_dropped_wifi_link_lights_the_top_left_pixel_red);
  RUN_TEST(test_a_dropped_broker_lights_the_bottom_left_pixel_yellow);
  RUN_TEST(test_mqtt_stays_dark_when_it_is_only_down_for_want_of_wifi);
  RUN_TEST(test_an_unconfigured_link_is_not_a_fault);
  RUN_TEST(test_a_link_still_dialling_in_shows_the_dot);
  RUN_TEST(test_the_mqtt_pixel_follows_the_matrix_height);
  RUN_TEST(test_the_dots_pulse_rather_than_sit_at_full_brightness);
  RUN_TEST(test_a_zero_period_leaves_the_colour_alone);
  return UNITY_END();
}
