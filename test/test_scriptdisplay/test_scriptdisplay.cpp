#include <unity.h>

#include <string>

#include "core/RuntimeState.h"
#include "core/apps/IApp.h"
#include "core/render/Canvas.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

static script::ScriptServices g_svc;
static RuntimeState g_runtime;
static int g_powerCalls;
static bool g_lastPower;
static bool g_powerAccepted;

void setUp() {
  g_svc = script::ScriptServices{};
  g_runtime = RuntimeState{};
  g_powerCalls = 0;
  g_lastPower = true;
  g_powerAccepted = true;
  g_svc.runtime = [] { return &g_runtime; };
  g_svc.setDisplayPower = [](bool on) {
    ++g_powerCalls;
    g_lastPower = on;
    return g_powerAccepted;
  };
  script::setServices(&g_svc);
}

void tearDown() { script::setServices(nullptr); }

static bool load(script::BerryVM& vm, const char* source) {
  std::string err;
  if (!script::installBindings(vm, err)) {
    TEST_MESSAGE(err.c_str());
    return false;
  }
  if (!vm.load(source)) {
    TEST_MESSAGE(vm.lastError().c_str());
    return false;
  }
  return true;
}

static void test_is_on_reads_the_current_runtime_state() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(
      vm, "def draw() pixel(0, 0, display.is_on() ? 0x00FF00 : 0xFF0000) end"));
  Canvas canvas(32, 8);
  RenderCtx ctx;
  script::BindingScope scope(&canvas, &ctx, "Nightmode");

  g_runtime.matrixOff = false;
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, canvas.getPixel(0, 0));

  g_runtime.matrixOff = true;
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(0, 0));
}

static void test_power_routes_the_requested_state_and_reports_acceptance() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(
      vm, "def draw() pixel(0, 0, display.power(false) ? 0x00FF00 : 0xFF0000) end"));
  Canvas canvas(32, 8);
  RenderCtx ctx;
  script::BindingScope scope(&canvas, &ctx, "Nightmode");

  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_INT(1, g_powerCalls);
  TEST_ASSERT_FALSE(g_lastPower);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, canvas.getPixel(0, 0));

  g_powerAccepted = false;
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_INT(2, g_powerCalls);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(0, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_is_on_reads_the_current_runtime_state);
  RUN_TEST(test_power_routes_the_requested_state_and_reports_acceptance);
  return UNITY_END();
}
