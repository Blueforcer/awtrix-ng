#include <unity.h>
#include "hal/GalacticUnicornBoard.cpp"

using namespace awtrix;
void setUp() {}
void tearDown() {}

void geometry_and_defaults() {
  auto cfg = galacticUnicornDefaults();
  GalacticUnicornBoard board(cfg);
  TEST_ASSERT_EQUAL(53, board.matrixWidth());
  TEST_ASSERT_EQUAL(11, board.matrixHeight());
  TEST_ASSERT_FALSE(cfg.scriptingEnabled);
  cfg.panelHeight = 8;
  cfg.panelWidth = 32;
  GalacticUnicornBoard letterbox(cfg);
  TEST_ASSERT_EQUAL(53, letterbox.matrixWidth());
  TEST_ASSERT_EQUAL(8, letterbox.matrixHeight());
  cfg.panelHeight = 16;
  TEST_ASSERT_EQUAL(11, GalacticUnicornBoard(cfg).matrixHeight());
}
void peripherals_are_not_claimed() {
  GalacticUnicornBoard board;
  board.begin();
  TEST_ASSERT_FALSE(board.hasBattery());
  TEST_ASSERT_FALSE(board.hasLightSensor());
  TEST_ASSERT_FALSE(board.sensors().hasSensor());
  TEST_ASSERT_FALSE(board.sensors().hasHumidity());
  TEST_ASSERT_NULL(board.toneSink());
  TEST_ASSERT_NULL(board.trackSink());
  TEST_ASSERT_EQUAL(-1, board.readBatteryMillivolts());
  ButtonState buttons{true, true, true};
  board.pollButtons(buttons);
  TEST_ASSERT_FALSE(buttons.left || buttons.select || buttons.right);
  Canvas canvas(53, 11);
  board.show(canvas);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(geometry_and_defaults);
  RUN_TEST(peripherals_are_not_claimed);
  return UNITY_END();
}
