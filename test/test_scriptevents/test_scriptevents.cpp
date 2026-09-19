#include <unity.h>

#include <string>

#include "core/apps/AppRegistry.h"
#include "core/script/ScriptHost.h"

using namespace awtrix;

static int64_t now;
static script::ScriptServices services;

void setUp() {
  now = 0;
  services = {};
  services.monotonicMs = [] { return now; };
}

void tearDown() {}

static std::string source(const std::string& body) {
  return "class App\nvar trace, id, other\ndef init() self.trace = '' end\n"
         "def draw() end\ndef check() return self.trace end\n" + body + "\nend\nreturn App()";
}

static std::string trace(AppRegistry& registry, const char* name = "A") {
  auto* app = static_cast<script::ScriptApp*>(registry.find(name));
  TEST_ASSERT_NOT_NULL(app);
  std::string result;
  TEST_ASSERT_TRUE(app->callCheckForTest(result));
  return result;
}

static void tick(script::ScriptHost& host, int64_t ms, const char* visible = "A") {
  now = ms;
  RenderCtx ctx;
  ctx.nowMs = ms;
  host.tick(ctx, visible);
}

static void test_timer_deadlines_repeat_and_cancel() {
  AppRegistry registry;
  script::ScriptHost host(registry, services, nullptr, nullptr);
  TEST_ASSERT_TRUE(host.set("A", source(
      "def setup()\n"
      " self.id = timer.every(50, / -> self.step())\n"
      " timer.after(25, / -> self.once())\nend\n"
      "def once() self.trace += 'a' end\n"
      "def step() self.trace += 'b'\n"
      " if size(self.trace) == 3 timer.cancel(self.id) end\nend")));
  tick(host, 24);
  TEST_ASSERT_EQUAL_STRING("", trace(registry).c_str());
  tick(host, 25);
  TEST_ASSERT_EQUAL_STRING("a", trace(registry).c_str());
  tick(host, 50);
  TEST_ASSERT_EQUAL_STRING("ab", trace(registry).c_str());
  tick(host, 5000);
  TEST_ASSERT_EQUAL_STRING("abb", trace(registry).c_str());
  tick(host, 6000);
  TEST_ASSERT_EQUAL_STRING("abb", trace(registry).c_str());
  TEST_ASSERT_TRUE(host.errorOf("A").message.empty());
}

static void test_timer_validation_limits_and_owner_isolation() {
  AppRegistry registry;
  script::ScriptHost host(registry, services, nullptr, nullptr);
  host.set("A", source(
      "def setup()\n"
      " for ms : [-1, 0, 24, 86400001, 25.5, '25', nil]\n"
      "  if timer.after(ms, / -> nil) != nil raise 'bad_delay' end\n end\n"
      " if timer.after(25, nil) != nil raise 'bad_callback' end\n"
      " for n : 1..8 self.id = timer.after(25, / -> self.fire()) end\n"
      " if timer.after(25, / -> nil) != nil raise 'limit' end\n"
      " shared.set('id', self.id)\nend\n"
      "def fire() self.trace += 'a' end"));
  host.set("B", source(
      "def setup()\n"
      " if timer.cancel(shared.get('A.id')) raise 'other_owner' end\n"
      " if timer.cancel(nil) || timer.cancel(-1) raise 'bad_cancel' end\nend"));
  tick(host, 25);
  TEST_ASSERT_EQUAL_STRING("aaaaaaaa", trace(registry).c_str());
  TEST_ASSERT_TRUE(host.errorOf("A").message.empty());
  TEST_ASSERT_TRUE(host.errorOf("B").message.empty());
}

static void test_timer_global_limit_and_replacement_cleanup() {
  AppRegistry registry;
  script::ScriptHost host(registry, services, nullptr, nullptr);
  const auto full = source("def setup() for n : 1..8 timer.after(25, / -> nil) end end");
  for (const char* name : {"A", "B", "C", "D"}) host.set(name, full);
  const auto probe = source(
      "def setup() self.trace = str(timer.after(25, / -> nil) == nil) end");
  host.set("E", probe);
  TEST_ASSERT_EQUAL_STRING("true", trace(registry, "E").c_str());
  host.set("A", source(""));
  host.set("E", probe);
  TEST_ASSERT_EQUAL_STRING("false", trace(registry, "E").c_str());
  host.remove("B");
  host.set("B", full);
  TEST_ASSERT_TRUE(host.errorOf("B").message.empty());
}

static void test_timer_cancel_due_and_reschedule_waits_for_next_tick() {
  AppRegistry registry;
  script::ScriptHost host(registry, services, nullptr, nullptr);
  host.set("A", source(
      "def setup()\n"
      " timer.after(25, / -> self.first())\n"
      " self.other = timer.after(25, / -> self.second())\nend\n"
      "def first()\n self.trace += 'a'\n timer.cancel(self.other)\n"
      " timer.after(25, / -> self.second())\nend\n"
      "def second() self.trace += 'b' end"));
  tick(host, 1000);
  TEST_ASSERT_EQUAL_STRING("a", trace(registry).c_str());
  tick(host, 1024);
  TEST_ASSERT_EQUAL_STRING("a", trace(registry).c_str());
  tick(host, 1025);
  TEST_ASSERT_EQUAL_STRING("ab", trace(registry).c_str());
}

static void test_timer_hidden_disabled_and_long_uptime() {
  now = INT64_C(5000000000);
  AppRegistry registry;
  script::ScriptHost host(registry, services, nullptr, nullptr);
  host.set("A", "# @headless true\n" + source(
      "def setup() timer.every(25, / -> self.fire()) end\n"
      "def fire() self.trace += 'a' end"));
  host.setRunningScripts({"A"});
  tick(host, INT64_C(5000000025), "Time");
  TEST_ASSERT_EQUAL_STRING("a", trace(registry).c_str());
  host.setRunningScripts({});
  tick(host, INT64_C(5000001000), "Time");
  TEST_ASSERT_EQUAL_STRING("a", trace(registry).c_str());
  host.setRunningScripts({"A"});
  tick(host, INT64_C(5000001001), "Time");
  TEST_ASSERT_EQUAL_STRING("aa", trace(registry).c_str());
  tick(host, INT64_C(5000001002), "Time");
  TEST_ASSERT_EQUAL_STRING("aa", trace(registry).c_str());
}

static void test_timer_failure_is_contained_and_frees_capacity() {
  AppRegistry registry;
  script::ScriptHost host(registry, services, nullptr, nullptr);
  host.set("A", source(
      "def setup()\n timer.after(25, / -> self.fail())\n"
      " for n : 1..7 timer.after(86400000, / -> nil) end\nend\n"
      "def fail() while true end end"));
  host.set("B", source(
      "def setup() timer.every(25, / -> self.fire()) end\n"
      "def fire() self.trace += 'b' end"));
  tick(host, 25);
  TEST_ASSERT_FALSE(host.errorOf("A").message.empty());
  TEST_ASSERT_EQUAL_STRING("b", trace(registry, "B").c_str());
  tick(host, 50);
  TEST_ASSERT_EQUAL_STRING("bb", trace(registry, "B").c_str());
  for (const char* name : {"C", "D", "E"})
    host.set(name, source("def setup() for n : 1..8 timer.after(25, / -> nil) end end"));
  host.set("F", source(
      "def setup() self.trace = str(timer.after(25, / -> nil) != nil) end"));
  TEST_ASSERT_EQUAL_STRING("true", trace(registry, "F").c_str());
}

static const std::string gestures =
    "def on_button_event(btn, event)\n"
    " self.trace += btn + ':' + event + ','\n return btn == 'select'\nend\n"
    "def on_button(btn) self.trace += 'legacy:' + btn + ','\n return true end";

static void test_button_capture_long_repeat_release_and_legacy_fallback() {
  AppRegistry registry;
  script::ScriptHost host(registry, services, nullptr, nullptr);
  host.set("A", source(gestures));
  tick(host, 0);
  TEST_ASSERT_TRUE(host.handleButtonState("A", 1, true));
  now = 599;
  host.handleButtonState("A", 1, true);
  TEST_ASSERT_EQUAL_STRING("select:press,", trace(registry).c_str());
  now = 600;
  host.handleButtonState("A", 1, true);
  now = 749;
  host.handleButtonState("A", 1, true);
  now = 750;
  host.handleButtonState("A", 1, true);
  now = 10000;
  host.handleButtonState("A", 1, true);
  host.handleButtonState("A", 1, false);
  host.handleButtonState("A", 1, false);
  TEST_ASSERT_EQUAL_STRING("select:press,select:long,select:repeat,select:repeat,select:release,",
                           trace(registry).c_str());
  TEST_ASSERT_TRUE(host.handleButtonState("A", 0, true));
  host.handleButtonState("A", 0, false);
  TEST_ASSERT_EQUAL_STRING(
      "select:press,select:long,select:repeat,select:repeat,select:release,left:press,legacy:left,",
      trace(registry).c_str());
}

static void test_button_capture_does_not_transfer_after_switch_or_replace() {
  AppRegistry registry;
  script::ScriptHost host(registry, services, nullptr, nullptr);
  host.set("A", source(gestures));
  host.set("B", source(gestures));
  tick(host, 0);
  host.handleButtonState("A", 1, true);
  tick(host, 100, "B");
  tick(host, 200, "A");
  now = 800;
  host.handleButtonState("A", 1, true);
  host.handleButtonState("A", 1, false);
  TEST_ASSERT_EQUAL_STRING("select:press,", trace(registry).c_str());
  TEST_ASSERT_EQUAL_STRING("", trace(registry, "B").c_str());
  host.handleButtonState("A", 1, true);
  host.set("A", source(gestures));
  tick(host, 1600);
  host.handleButtonState("A", 1, true);
  host.handleButtonState("A", 1, false);
  TEST_ASSERT_EQUAL_STRING("", trace(registry).c_str());
}

static void test_button_disabled_error_and_legacy_only() {
  AppRegistry registry;
  script::ScriptHost host(registry, services, nullptr, nullptr);
  host.set("A", source(gestures));
  tick(host, 0);
  host.handleButtonState("A", 1, true);
  host.setRunningScripts({});
  host.setRunningScripts({"A"});
  now = 1000;
  host.handleButtonState("A", 1, false);
  TEST_ASSERT_EQUAL_STRING("select:press,", trace(registry).c_str());
  host.set("A", source("def on_button_event(b, e) raise 'broken' end"));
  tick(host, 1001);
  TEST_ASSERT_FALSE(host.handleButtonState("A", 1, true));
  TEST_ASSERT_FALSE(host.errorOf("A").message.empty());
  host.handleButtonState("A", 1, false);
  host.set("A", source("def on_button(b) self.trace += b return true end"));
  tick(host, 1002);
  TEST_ASSERT_TRUE(host.handleButtonState("A", 1, true));
  now = 2000;
  host.handleButtonState("A", 1, true);
  host.handleButtonState("A", 1, false);
  TEST_ASSERT_EQUAL_STRING("select", trace(registry).c_str());
  TEST_ASSERT_FALSE(host.handleButtonState("A", 3, true));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_timer_deadlines_repeat_and_cancel);
  RUN_TEST(test_timer_validation_limits_and_owner_isolation);
  RUN_TEST(test_timer_global_limit_and_replacement_cleanup);
  RUN_TEST(test_timer_cancel_due_and_reschedule_waits_for_next_tick);
  RUN_TEST(test_timer_hidden_disabled_and_long_uptime);
  RUN_TEST(test_timer_failure_is_contained_and_frees_capacity);
  RUN_TEST(test_button_capture_long_repeat_release_and_legacy_fallback);
  RUN_TEST(test_button_capture_does_not_transfer_after_switch_or_replace);
  RUN_TEST(test_button_disabled_error_and_legacy_only);
  return UNITY_END();
}
