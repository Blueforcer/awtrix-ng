#include <unity.h>

#include <string>

#include "core/RuntimeState.h"
#include "core/apps/IApp.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

static script::ScriptServices g_svc;
static RuntimeState g_rt;
static std::string g_log;

void setUp() {
  g_rt = RuntimeState{};
  g_log.clear();
  g_svc = script::ScriptServices{};
  g_svc.log = [](const std::string& s) { g_log += s; };
  script::setServices(&g_svc);
}
void tearDown() { script::setServices(nullptr); }

static std::string run(const char* body) {
  script::BerryVM vm;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(script::installBindings(vm, err), err.c_str());
  const std::string src = std::string("def draw() ") + body + " end";
  TEST_ASSERT_TRUE_MESSAGE(vm.load(src.c_str()), vm.lastError().c_str());
  RenderCtx ctx;
  ctx.runtime = &g_rt;
  script::BindingScope s(nullptr, &ctx, "T");
  TEST_ASSERT_TRUE_MESSAGE(vm.call("draw"), vm.lastError().c_str());
  return g_log;
}

static bool logged(const char* needle) { return g_log.find(needle) != std::string::npos; }

static void test_readings_reach_the_script() {
  g_rt.temperatureC = 21.5f;
  g_rt.humidity = 48.0f;
  g_rt.pressureHpa = 1013.0f;
  g_rt.lightLevel = 72.0f;
  g_rt.batteryPercent = 63;
  g_rt.batteryVoltage = 3.9f;
  g_rt.hasPressure = true;
  g_rt.hasLightSensor = true;

  run("log(str(sensor.temperature()) + '/' + str(sensor.humidity()) + '/' + "
      "str(sensor.pressure()) + '/' + str(sensor.light()) + '/' + "
      "str(sensor.battery()) + '/' + str(sensor.battery_volts()))");
  TEST_ASSERT_TRUE(logged("21.5/48/1013/72/63/3.9"));
}

static void test_a_missing_sensor_answers_nil() {
  g_rt.temperatureC = 21.5f;
  g_rt.humidity = 48.0f;
  g_rt.hasTemperature = false;
  g_rt.hasHumidity = false;
  run("log(str(sensor.temperature()) + '/' + str(sensor.humidity()) + '/' + "
      "str(sensor.pressure()) + '/' + str(sensor.light()))");
  TEST_ASSERT_TRUE(logged("nil/nil/nil/nil"));
}

static void test_battery_is_a_whole_percent() {
  g_rt.batteryPercent = 7;
  run("log(str(sensor.battery()))");
  TEST_ASSERT_TRUE(logged("7"));
  TEST_ASSERT_FALSE(logged("7."));
}

static void test_a_missing_battery_answers_nil() {
  g_rt.batteryPercent = 63;
  g_rt.hasBattery = false;
  run("log(str(sensor.battery()) + '/' + str(sensor.battery_volts()))");
  TEST_ASSERT_TRUE(logged("nil/nil"));
}

static void test_without_a_context_everything_is_nil() {
  script::BerryVM vm;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(script::installBindings(vm, err), err.c_str());
  TEST_ASSERT_TRUE(vm.load("def draw() log(str(sensor.temperature()) + '/' + "
                           "str(sensor.battery()) + '/' + str(sensor.light())) end"));
  script::BindingScope s(nullptr, nullptr, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_TRUE(logged("nil/nil/nil"));
}

static void test_negative_temperature_survives() {
  g_rt.temperatureC = -7.5f;
  run("log(str(sensor.temperature()))");
  TEST_ASSERT_TRUE(logged("-7.5"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_readings_reach_the_script);
  RUN_TEST(test_a_missing_sensor_answers_nil);
  RUN_TEST(test_battery_is_a_whole_percent);
  RUN_TEST(test_a_missing_battery_answers_nil);
  RUN_TEST(test_without_a_context_everything_is_nil);
  RUN_TEST(test_negative_temperature_survives);
  return UNITY_END();
}
