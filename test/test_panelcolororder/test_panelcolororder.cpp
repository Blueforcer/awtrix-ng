#include <unity.h>

#include "core/render/PanelColorOrder.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

namespace {

void assertDriverColor(PanelColorOrder order, uint8_t r, uint8_t g, uint8_t b) {
  const render::DriverColor actual = render::colorForGrbDriver(0x112233u, order);
  TEST_ASSERT_EQUAL_HEX8(r, actual.r);
  TEST_ASSERT_EQUAL_HEX8(g, actual.g);
  TEST_ASSERT_EQUAL_HEX8(b, actual.b);
}

}

static void test_every_panel_order_is_encoded_for_the_fixed_grb_driver() {
  assertDriverColor(PanelColorOrder::Rgb, 0x22, 0x11, 0x33);
  assertDriverColor(PanelColorOrder::Rbg, 0x33, 0x11, 0x22);
  assertDriverColor(PanelColorOrder::Grb, 0x11, 0x22, 0x33);
  assertDriverColor(PanelColorOrder::Gbr, 0x33, 0x22, 0x11);
  assertDriverColor(PanelColorOrder::Brg, 0x11, 0x33, 0x22);
  assertDriverColor(PanelColorOrder::Bgr, 0x22, 0x33, 0x11);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_panel_order_is_encoded_for_the_fixed_grb_driver);
  return UNITY_END();
}
