#include <unity.h>

#include <string>

#include "core/apps/IApp.h"
#include "core/render/Canvas.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

static script::ScriptServices g_svc;
static std::string g_lastJson;
static bool g_accept = true;
static int g_next = 0;
static int g_prev = 0;
static int g_holdCalls = 0;
static bool g_lastHold = false;
static std::string g_shown;
static bool g_showAccepts = true;

void setUp() {
  g_lastJson.clear();
  g_accept = true;
  g_next = 0;
  g_prev = 0;
  g_holdCalls = 0;
  g_lastHold = false;
  g_shown.clear();
  g_showAccepts = true;
  g_svc = script::ScriptServices{};
  g_svc.notify = [](const std::string& json) {
    g_lastJson = json;
    return g_accept;
  };
  g_svc.rotateNext = [] { ++g_next; };
  g_svc.rotatePrevious = [] { ++g_prev; };
  g_svc.holdRotation = [](bool p) {
    ++g_holdCalls;
    g_lastHold = p;
  };
  g_svc.showApp = [](const std::string& id) {
    g_shown = id;
    return g_showAccepts;
  };
  script::setServices(&g_svc);
}
void tearDown() { script::setServices(nullptr); }

static bool load(script::BerryVM& vm, const char* user) {
  std::string err;
  if (!script::installBindings(vm, err)) {
    TEST_MESSAGE(err.c_str());
    return false;
  }
  if (!vm.load(user)) {
    TEST_MESSAGE(vm.lastError().c_str());
    return false;
  }
  return true;
}

static bool jsonHas(const char* needle) {
  return g_lastJson.find(needle) != std::string::npos;
}

static void call(const char* user) {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(vm, user));
  script::BindingScope s(nullptr, nullptr, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
}

static void test_map_is_serialised_to_payload_json() {
  call("def draw() notify({'text': 'Hi', 'icon': '1234', 'hold': true}) end");
  TEST_ASSERT_TRUE(jsonHas("\"text\":\"Hi\""));
  TEST_ASSERT_TRUE(jsonHas("\"icon\":\"1234\""));
  TEST_ASSERT_TRUE(jsonHas("\"hold\":true"));
}

static void test_integer_colour_serialises_as_number() {
  call("def draw() notify({'text': 'x', 'textColor': 0xFF0000}) end");
  TEST_ASSERT_TRUE(jsonHas("16711680"));
  TEST_ASSERT_FALSE(jsonHas("\"textColor\":\"") );
}

static void test_sound_keys_pass_through() {
  call("def draw() notify({'text': 'Ding', 'soundRtttl': 'x:d=4,o=5,b=100:c', 'soundLoop': true}) end");
  TEST_ASSERT_TRUE(jsonHas("\"soundRtttl\":\"x:d=4,o=5,b=100:c\""));
  TEST_ASSERT_TRUE(jsonHas("\"soundLoop\":true"));
}

static void test_return_value_reflects_acceptance() {
  g_accept = false;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(vm, "def draw() pixel(0, 0, notify({'text':'x'}) ? 0xFF0000 : 0x00FF00) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
}

static void test_notify_without_service_returns_false() {
  g_svc.notify = nullptr;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(vm, "def draw() pixel(0, 0, notify({'text':'x'}) ? 0xFF0000 : 0x00FF00) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
}

static void test_rotation_controls_reach_the_service() {
  call("def draw() rotation.next() end");
  TEST_ASSERT_EQUAL_INT(1, g_next);
  TEST_ASSERT_EQUAL_INT(0, g_prev);

  call("def draw() rotation.previous() end");
  TEST_ASSERT_EQUAL_INT(1, g_prev);

  call("def draw() rotation.pause() end");
  TEST_ASSERT_EQUAL_INT(1, g_holdCalls);
  TEST_ASSERT_TRUE(g_lastHold);

  call("def draw() rotation.resume() end");
  TEST_ASSERT_EQUAL_INT(2, g_holdCalls);
  TEST_ASSERT_FALSE(g_lastHold);
}

static void test_rotation_controls_without_service_are_harmless() {
  g_svc.rotateNext = nullptr;
  g_svc.rotatePrevious = nullptr;
  g_svc.holdRotation = nullptr;
  g_svc.showApp = nullptr;
  call("def draw() rotation.next() rotation.previous() rotation.pause() rotation.resume() "
       "rotation.show() end");
  TEST_ASSERT_EQUAL_INT(0, g_next);
  TEST_ASSERT_EQUAL_INT(0, g_prev);
  TEST_ASSERT_EQUAL_INT(0, g_holdCalls);
  TEST_ASSERT_TRUE(g_shown.empty());
}

static void test_rotation_show_names_the_calling_script() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(vm, "def draw() pixel(0, 0, rotation.show() ? 0xFF0000 : 0x00FF00) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  script::BindingScope s(&c, &ctx, "Nightmode");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_STRING("Nightmode", g_shown.c_str());
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));
}

static void test_rotation_show_reports_a_refusal() {
  g_showAccepts = false;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(vm, "def draw() pixel(0, 0, rotation.show() ? 0xFF0000 : 0x00FF00) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  script::BindingScope s(&c, &ctx, "Hidden");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_map_is_serialised_to_payload_json);
  RUN_TEST(test_integer_colour_serialises_as_number);
  RUN_TEST(test_sound_keys_pass_through);
  RUN_TEST(test_return_value_reflects_acceptance);
  RUN_TEST(test_notify_without_service_returns_false);
  RUN_TEST(test_rotation_controls_reach_the_service);
  RUN_TEST(test_rotation_controls_without_service_are_harmless);
  RUN_TEST(test_rotation_show_names_the_calling_script);
  RUN_TEST(test_rotation_show_reports_a_refusal);
  return UNITY_END();
}
