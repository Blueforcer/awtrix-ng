#include <unity.h>
#include "../ScriptApplication.h"

#include <string>

#include "core/apps/IApp.h"
#include "core/render/Canvas.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

static script::ScriptServices g_svc;
static awtrix::test::ScriptApplication application;
static std::string g_lastJson;
static std::string g_lastScript;
static bool g_accept = true;
static int g_next = 0;
static int g_prev = 0;
static int g_holdCalls = 0;
static bool g_lastHold = false;
static std::string g_shown;
static bool g_showAccepts = true;
static std::string g_closed;

void setUp() {
  g_lastJson.clear();
  g_lastScript.clear();
  g_accept = true;
  g_next = 0;
  g_prev = 0;
  g_holdCalls = 0;
  g_lastHold = false;
  g_shown.clear();
  g_showAccepts = true;
  g_closed.clear();
  g_svc = script::ScriptServices{};
  application = {};
  g_svc.application = &application;
  application.notifyFn = [](const std::string& json, const std::string& script) {
    g_lastJson = json;
    g_lastScript = script;
    return g_accept;
  };
  application.rotateNextFn = [] { ++g_next; };
  application.rotatePreviousFn = [] { ++g_prev; };
  application.holdRotationFn = [](bool p) {
    ++g_holdCalls;
    g_lastHold = p;
  };
  application.showAppFn = [](const std::string& id) {
    g_shown = id;
    return g_showAccepts;
  };
  application.closeSessionFn = [](const std::string& id) {
    g_closed = id;
    return id == "Game";
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
  call("def draw() notify({'text': 'x', 'sound': {'rtttl': 'a:d=4,o=5,b=120:c', 'loop': true}}) "
       "end");
  TEST_ASSERT_TRUE(jsonHas("\"sound\":{"));
  TEST_ASSERT_TRUE(jsonHas("\"rtttl\":\"a:d=4,o=5,b=120:c\""));
  TEST_ASSERT_TRUE(jsonHas("\"loop\":true"));
}

// The sender comes from the scope, so a notification's sound is found in that script's folder.
static void test_the_notification_names_the_script_that_sent_it() {
  call("def draw() notify({'text': 'Lap', 'sound': 'boost'}) end");
  TEST_ASSERT_TRUE(jsonHas("\"sound\":\"boost\""));
  TEST_ASSERT_EQUAL_STRING("T", g_lastScript.c_str());
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
  application.notifyFn = nullptr;
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
  application.rotateNextFn = nullptr;
  application.rotatePreviousFn = nullptr;
  application.holdRotationFn = nullptr;
  application.showAppFn = nullptr;
  application.closeSessionFn = nullptr;
  call("def draw() rotation.next() rotation.previous() rotation.pause() rotation.resume() "
       "rotation.show() rotation.close() end");
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

static void test_rotation_close_names_the_calling_script() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(load(vm, "def draw() pixel(0, 0, rotation.close() ? 0xFF0000 : 0x00FF00) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  {
    script::BindingScope s(&c, &ctx, "Game");
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
  TEST_ASSERT_EQUAL_STRING("Game", g_closed.c_str());
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(0, 0));

  script::BindingScope s(&c, &ctx, "Clock");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(0, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_map_is_serialised_to_payload_json);
  RUN_TEST(test_integer_colour_serialises_as_number);
  RUN_TEST(test_sound_keys_pass_through);
  RUN_TEST(test_the_notification_names_the_script_that_sent_it);
  RUN_TEST(test_return_value_reflects_acceptance);
  RUN_TEST(test_notify_without_service_returns_false);
  RUN_TEST(test_rotation_controls_reach_the_service);
  RUN_TEST(test_rotation_controls_without_service_are_harmless);
  RUN_TEST(test_rotation_show_names_the_calling_script);
  RUN_TEST(test_rotation_show_reports_a_refusal);
  RUN_TEST(test_rotation_close_names_the_calling_script);
  return UNITY_END();
}
