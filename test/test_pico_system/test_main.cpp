#include <unity.h>
#include <limits>
#include "system/TimeConfig.h"
#include "system/PicoSleep.h"
#include "system/PicoResetReason.h"
#include "system/DevicePageServices.h"

void setUp() {}
void tearDown() {}
static void time_config() {
  awtrix::TimeConfig c;
  TEST_ASSERT_TRUE(c.update("UTC0", "pool.ntp.org"));
  const char* owned = c.server().c_str();
  TEST_ASSERT_FALSE(c.update("UTC0", "pool.ntp.org"));
  TEST_ASSERT_EQUAL_PTR(owned, c.server().c_str());
  TEST_ASSERT_TRUE(c.update("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org"));
  TEST_ASSERT_TRUE(c.update("EST5EDT,M3.2.0,M11.1.0", "time.example"));
  TEST_ASSERT_FALSE(c.needsUpdate(c.timezone(), c.server()));
  TEST_ASSERT_TRUE(c.update(c.timezone(), c.server(), true));
  TEST_ASSERT_EQUAL_STRING("time.example", c.server().c_str());
}
static void clock_state() {
  awtrix::RenderCtx ctx;
  awtrix::DevicePageClock::fillAt(ctx, 42, 0);
  TEST_ASSERT_EQUAL_INT64(-1, ctx.epochMs);
  TEST_ASSERT_EQUAL_INT64(42, ctx.nowMs);
  std::tm local{};
  local.tm_year = 124; local.tm_mon = 6; local.tm_mday = 1;
  local.tm_hour = 12; local.tm_min = 34; local.tm_isdst = -1;
  const int64_t epochMs = static_cast<int64_t>(std::mktime(&local)) * 1000 + 321;
  awtrix::DevicePageClock::fillAt(ctx, 45, epochMs);
  TEST_ASSERT_EQUAL_INT64(epochMs, ctx.epochMs);
  TEST_ASSERT_EQUAL_INT(2024, ctx.year);
  TEST_ASSERT_EQUAL_INT(7, ctx.month);
  TEST_ASSERT_EQUAL_INT(1, ctx.mday);
  TEST_ASSERT_EQUAL_INT(12, ctx.hour);
  TEST_ASSERT_EQUAL_INT(34, ctx.minute);
}
static void reset_mapping() {
  enum class R { UNKNOWN_RESET, PWRON_RESET, RUN_PIN_RESET, SOFT_RESET,
                 WDT_RESET, DEBUG_RESET, GLITCH_RESET, BROWNOUT_RESET };
  using awtrix::platform::picoResetReasonName;
  TEST_ASSERT_EQUAL_STRING("poweron", picoResetReasonName(R::PWRON_RESET));
  TEST_ASSERT_EQUAL_STRING("external", picoResetReasonName(R::RUN_PIN_RESET));
  TEST_ASSERT_EQUAL_STRING("software", picoResetReasonName(R::SOFT_RESET));
  TEST_ASSERT_EQUAL_STRING("watchdog", picoResetReasonName(R::WDT_RESET));
  TEST_ASSERT_EQUAL_STRING("external", picoResetReasonName(R::DEBUG_RESET));
  TEST_ASSERT_EQUAL_STRING("brownout", picoResetReasonName(R::BROWNOUT_RESET));
  TEST_ASSERT_EQUAL_STRING("unknown", picoResetReasonName(R::GLITCH_RESET));
  TEST_ASSERT_EQUAL_STRING("unknown", picoResetReasonName(R::UNKNOWN_RESET));
}
static void sleep_wake() {
  awtrix::PicoSleep timer(100, 1000, false);
  TEST_ASSERT_FALSE(timer.wake(1099, false));
  TEST_ASSERT_TRUE(timer.wake(1100, false));
  awtrix::PicoSleep held(100, 1000, true);
  TEST_ASSERT_FALSE(held.wake(110, true));
  TEST_ASSERT_FALSE(held.wake(120, false));
  TEST_ASSERT_TRUE(held.wake(130, true));
  awtrix::PicoSleep press(100, 1000, false);
  TEST_ASSERT_TRUE(press.wake(101, true));
  awtrix::PicoSleep longSleep(100, std::numeric_limits<uint64_t>::max(), false);
  TEST_ASSERT_FALSE(longSleep.wake(1000, false));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(time_config);
  RUN_TEST(clock_state);
  RUN_TEST(reset_mapping);
  RUN_TEST(sleep_wake);
  return UNITY_END();
}
