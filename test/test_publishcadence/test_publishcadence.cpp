#include <unity.h>

#include "core/StatePublishCadence.h"

using namespace awtrix;

namespace {

StatePublishCadence fresh() {
  StatePublishCadence c;
  c.configure(10000);
  c.settingsDue();
  c.stateDue(100000);
  return c;
}

void test_app_publishes_once_per_change() {
  StatePublishCadence c = fresh();
  TEST_ASSERT_TRUE(c.appDue("Time"));
  TEST_ASSERT_FALSE(c.appDue("Time"));
  TEST_ASSERT_TRUE(c.appDue("Date"));
  TEST_ASSERT_TRUE(c.appDue("Time"));
}

void test_settings_publish_on_change_only() {
  StatePublishCadence c = fresh();
  TEST_ASSERT_FALSE(c.settingsDue());
  c.onEvent(StateEvent::SettingsChanged);
  TEST_ASSERT_TRUE(c.settingsDue());
  TEST_ASSERT_FALSE(c.settingsDue());
}

void test_state_follows_the_interval() {
  StatePublishCadence c = fresh();
  TEST_ASSERT_FALSE(c.stateDue(105000));
  TEST_ASSERT_FALSE(c.stateDue(109999));
  TEST_ASSERT_TRUE(c.stateDue(110000));
  TEST_ASSERT_FALSE(c.stateDue(110001));
}

void test_power_and_indicator_pull_the_publish_forward() {
  StatePublishCadence c = fresh();
  c.onEvent(StateEvent::PowerChanged);
  TEST_ASSERT_TRUE(c.stateDue(100300));
  TEST_ASSERT_FALSE(c.stateDue(100400));

  c.onEvent(StateEvent::IndicatorChanged);
  TEST_ASSERT_TRUE(c.stateDue(100700));
}

void test_forced_publishes_are_spaced_by_the_floor() {
  StatePublishCadence c = fresh();
  c.onEvent(StateEvent::PowerChanged);
  TEST_ASSERT_TRUE(c.stateDue(100300));
  c.onEvent(StateEvent::IndicatorChanged);
  TEST_ASSERT_FALSE(c.stateDue(100400));
  TEST_ASSERT_FALSE(c.stateDue(100549));
  TEST_ASSERT_TRUE(c.stateDue(100550));
}

void test_brightness_does_not_force_a_publish() {
  StatePublishCadence c = fresh();
  c.onEvent(StateEvent::BrightnessChanged);
  TEST_ASSERT_FALSE(c.stateDue(100300));
  TEST_ASSERT_FALSE(c.stateDue(105000));
  TEST_ASSERT_TRUE(c.stateDue(110000));
}

void test_reconnect_makes_everything_due_again() {
  StatePublishCadence c = fresh();
  c.appDue("Time");
  c.onConnect();
  TEST_ASSERT_TRUE(c.settingsDue());
  TEST_ASSERT_TRUE(c.appDue("Time"));
}

void test_buttons_publish_on_change_and_after_a_reconnect() {
  StatePublishCadence c = fresh();
  c.buttonsDue();
  TEST_ASSERT_FALSE(c.buttonsDue());

  c.onEvent(StateEvent::ButtonsChanged);
  TEST_ASSERT_TRUE(c.buttonsDue());
  TEST_ASSERT_FALSE(c.buttonsDue());

  c.onConnect();
  TEST_ASSERT_TRUE(c.buttonsDue());
  TEST_ASSERT_FALSE(c.buttonsDue());
}

void test_interval_is_floored_at_one_second() {
  StatePublishCadence c;
  c.configure(10);
  c.stateDue(100000);
  TEST_ASSERT_FALSE(c.stateDue(100999));
  TEST_ASSERT_TRUE(c.stateDue(101000));
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_app_publishes_once_per_change);
  RUN_TEST(test_settings_publish_on_change_only);
  RUN_TEST(test_state_follows_the_interval);
  RUN_TEST(test_power_and_indicator_pull_the_publish_forward);
  RUN_TEST(test_forced_publishes_are_spaced_by_the_floor);
  RUN_TEST(test_brightness_does_not_force_a_publish);
  RUN_TEST(test_reconnect_makes_everything_due_again);
  RUN_TEST(test_buttons_publish_on_change_and_after_a_reconnect);
  RUN_TEST(test_interval_is_floored_at_one_second);
  UNITY_END();
  return 0;
}
