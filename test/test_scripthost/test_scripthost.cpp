#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "core/RuntimeState.h"
#include "core/apps/AppRegistry.h"
#include "core/apps/IApp.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptApp.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptConfig.h"
#include "core/script/ScriptHeap.h"
#include "core/script/ScriptHeapTesting.h"
#include "core/script/ScriptHost.h"
#include "core/script/ScriptService.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

static long g_now = 0;
static script::ScriptServices g_svc;

void setUp() {
  g_now = 0;
  g_svc = {};
  g_svc.monotonicMs = [] { return g_now; };
}

void tearDown() { script::heap::testing::resetBudgetBytes(); }

static std::string app(const std::string& body) {
  return "class App\n" + body + "\nend\nreturn App()";
}

static std::string check(AppRegistry& reg, const char* name) {
  auto* app = static_cast<script::ScriptApp*>(reg.find(name));
  TEST_ASSERT_NOT_NULL(app);
  std::string out;
  TEST_ASSERT_TRUE(app->callCheckForTest(out));
  return out;
}

namespace {

struct FakeHttp : script::IScriptHttp {
  std::vector<uint32_t> ids;
  std::vector<std::string> urls;
  std::vector<std::string> methods;
  bool accept = true;
  bool request(const script::HttpRequest& req) override {
    if (!accept) return false;
    ids.push_back(req.id);
    urls.push_back(req.url);
    methods.push_back(req.method);
    return true;
  }
  uint32_t last() const { return ids.empty() ? 0 : ids.back(); }
};

struct FakeMqtt : script::IScriptMqtt {
  std::vector<std::string> subscribed;
  std::vector<std::string> unsubscribed;
  std::vector<std::string> published;
  void publish(const std::string& topic, const std::string& payload) override {
    published.push_back(topic + "=" + payload);
  }
  void subscribe(const std::string& topic) override { subscribed.push_back(topic); }
  void unsubscribeAll(const std::string& topic) override { unsubscribed.push_back(topic); }
};

struct FakeSink : script::IScriptStoreSink {
  std::vector<std::string> writes;
  void storeChanged(const std::string& s, const std::string& json) override {
    writes.push_back(s + ":" + json);
  }
};

}


static void test_set_replace_remove_registry_and_hooks() {
  AppRegistry reg;
  std::vector<std::string> installed, removed;
  script::ScriptHost host(
      reg, g_svc, [&](const std::string& id) { installed.push_back(id); },
      [&](const std::string& id) { removed.push_back(id); });

  TEST_ASSERT_TRUE(host.set("Clock", app("def draw() end")));
  TEST_ASSERT_NOT_NULL(reg.find("Clock"));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)installed.size());
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)host.count());

  TEST_ASSERT_TRUE(host.set("Clock", app("def draw() pixel(0,0,1) end")));
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)installed.size());
  TEST_ASSERT_EQUAL_STRING("Clock", installed[1].c_str());
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)host.count());
  TEST_ASSERT_NOT_NULL(reg.find("Clock"));

  host.remove("Clock");
  TEST_ASSERT_NULL(reg.find("Clock"));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)removed.size());
  TEST_ASSERT_EQUAL_UINT(0u, (unsigned)host.count());
}

static void test_replace_points_registry_at_the_new_app() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("A", app("def draw() pixel(0, 0, 0x11) end"));
  host.set("A", app("def draw() pixel(0, 0, 0x22) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  reg.find("A")->render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0x22u, c.getPixel(0, 0));
}

// A script is refused only by the heap and fragmentation guards exercised below.
static void test_installing_many_scripts_succeeds() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  for (int i = 0; i < 24; ++i)
    TEST_ASSERT_TRUE(host.set("s" + std::to_string(i), app("def draw() end")));
  TEST_ASSERT_EQUAL_UINT(24u, (unsigned)host.count());
}

static void test_install_refused_over_the_script_heap_budget() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);

  script::heap::testing::setBudgetBytes(0);
  TEST_ASSERT_FALSE(host.set("A", app("def draw() end")));
  TEST_ASSERT_NULL(reg.find("A"));
  TEST_ASSERT_TRUE(host.lastRefusal().find("budget") != std::string::npos);
  TEST_ASSERT_TRUE(host.lastRefusal().find("internal") != std::string::npos);
}

static std::string fatApp() {
  std::string body;
  for (int i = 0; i < 40; ++i) {
    const std::string n = std::to_string(i);
    body += "def m" + n + "(a, b)\n  var t = a * " + n + " + b\n  var u = \"literal " + n +
            " padding\"\n  return t + size(u)\nend\n";
  }
  body += "def draw() end\n";
  return app(body);
}

static int installUntilRefused(std::size_t budget, int attempts, const std::string& src) {
  script::heap::testing::setBudgetBytes(budget);
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  int installed = 0;
  for (int i = 0; i < attempts; ++i) {
    if (!host.set("s" + std::to_string(i), src)) break;
    ++installed;
  }
  return installed;
}

static void test_a_larger_budget_admits_more_scripts() {
  const int kAttempts = 40;
  const std::string src = fatApp();

  const int tight = installUntilRefused(script::heap::testing::defaultBudgetBytes(), kAttempts, src);
  TEST_ASSERT_TRUE_MESSAGE(tight > 0, "the default budget must admit at least one script");
  TEST_ASSERT_TRUE_MESSAGE(tight < kAttempts, "the default budget must refuse before every attempt");

  const int roomy = installUntilRefused(64u * 1024 * 1024, kAttempts, src);
  TEST_ASSERT_TRUE_MESSAGE(roomy > tight, "a larger budget must admit strictly more scripts");
  TEST_ASSERT_EQUAL_INT_MESSAGE(kAttempts, roomy, "with a large budget every attempt succeeds");
}

static void test_budget_does_not_relax_the_compile_guards() {
  AppRegistry reg;
  std::size_t freeHeap = 0;
  g_svc.freeHeap = [&freeHeap] { return freeHeap; };
  script::heap::testing::setBudgetBytes(64u * 1024 * 1024);
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);

  const std::string src = app("def draw() end");
  freeHeap = script::installNeedsBytes(src.size()) - 1;
  TEST_ASSERT_FALSE(host.set("A", src));
  TEST_ASSERT_TRUE(host.lastRefusal().find("free memory") != std::string::npos);

  freeHeap = script::installNeedsBytes(src.size());
  TEST_ASSERT_TRUE(host.set("A", src));
}

static void test_install_refused_when_system_heap_is_low() {
  AppRegistry reg;
  std::size_t freeHeap = 0;
  g_svc.freeHeap = [&freeHeap] { return freeHeap; };
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);

  const std::string src = app("def draw() end");
  const std::size_t need = script::installNeedsBytes(src.size());

  freeHeap = need - 1;
  TEST_ASSERT_FALSE(host.set("A", src));
  TEST_ASSERT_NULL(reg.find("A"));
  TEST_ASSERT_TRUE(host.lastRefusal().find("free memory") != std::string::npos);

  freeHeap = need;
  TEST_ASSERT_TRUE(host.set("A", src));
  TEST_ASSERT_NOT_NULL(reg.find("A"));
  TEST_ASSERT_TRUE(host.lastRefusal().empty());

  const std::size_t replaceNeed = script::installNeedsBytes(src.size(), true);
  TEST_ASSERT_TRUE(replaceNeed < need);
  freeHeap = replaceNeed - 1;
  TEST_ASSERT_FALSE(host.set("A", src));
  TEST_ASSERT_NOT_NULL(reg.find("A"));
  TEST_ASSERT_TRUE(host.errorOf("A").empty());
  freeHeap = replaceNeed;
  TEST_ASSERT_TRUE(host.set("A", src));
}

static void test_install_refused_when_the_heap_is_too_fragmented() {
  AppRegistry reg;
  std::size_t largest = 0;
  g_svc.freeHeap = [] { return static_cast<std::size_t>(1024 * 1024); };
  g_svc.maxAllocHeap = [&largest] { return largest; };
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);

  const std::string src = app("def draw() end");

  largest = src.size() - 1;
  TEST_ASSERT_FALSE(host.set("A", src));
  TEST_ASSERT_NULL(reg.find("A"));
  TEST_ASSERT_TRUE(host.lastRefusal().find("contiguous") != std::string::npos);

  largest = src.size();
  TEST_ASSERT_TRUE(host.set("A", src));
  TEST_ASSERT_NOT_NULL(reg.find("A"));
}

static void test_low_heap_refusal_is_transient_while_a_fetch_is_in_flight() {
  FakeHttp fake;
  g_svc.http = &fake;
  AppRegistry reg;
  std::size_t freeHeap = 1024 * 1024;
  g_svc.freeHeap = [&freeHeap] { return freeHeap; };
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);

  const std::string src = app("def draw() end");
  const std::size_t tooLittle = script::installNeedsBytes(src.size()) - 1;

  freeHeap = tooLittle;
  TEST_ASSERT_FALSE(host.set("A", src));
  TEST_ASSERT_FALSE(host.refusalIsTransient());

  freeHeap = 1024 * 1024;
  TEST_ASSERT_TRUE(host.set("W", app("def setup() http.get('http://x/', def(b) end) end\n"
                                     "def draw() end")));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)fake.ids.size());

  freeHeap = tooLittle;
  TEST_ASSERT_FALSE(host.set("A", src));
  TEST_ASSERT_TRUE(host.refusalIsTransient());

  host.pushHttpResult({fake.last(), true, 200, ""});
  RenderCtx ctx;
  host.tick(ctx, "W");
  TEST_ASSERT_FALSE(host.set("A", src));
  TEST_ASSERT_FALSE(host.refusalIsTransient());
}

static void test_install_headroom_scales_with_source_size() {
  const std::size_t small = script::installNeedsBytes(500);
  const std::size_t big = script::installNeedsBytes(8 * 1024);
  TEST_ASSERT_TRUE(big > small);
  TEST_ASSERT_TRUE(small < script::kInstallHeadroomBytes + 2 * 1024);
}

static void test_install_not_blocked_without_a_heap_reading() {
  AppRegistry reg;
  g_svc.freeHeap = nullptr;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  TEST_ASSERT_TRUE(host.set("A", app("def draw() end")));
  TEST_ASSERT_NOT_NULL(reg.find("A"));
}

static void test_broken_script_still_installs() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  TEST_ASSERT_TRUE(host.set("Bad", app("def draw( end")));
  TEST_ASSERT_NOT_NULL(reg.find("Bad"));
  TEST_ASSERT_TRUE(host.errorOf("Bad").message.find("syntax_error") != std::string::npos);
  TEST_ASSERT_TRUE(host.errorOf("Unknown").empty());
}


static void test_tick_runs_loop_at_1hz_and_visibility() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("L", app("var n, vis\n"
                    "def init() self.n = 0 self.vis = '' end\n"
                    "def loop() self.n += 1 end\n"
                    "def draw() end\n"
                    "def on_show() self.vis += '+' end\n"
                    "def on_hide() self.vis += '-' end\n"
                    "def check() return str(self.n) .. self.vis end"));
  RenderCtx ctx;

  host.tick(ctx, "Time");
  TEST_ASSERT_EQUAL_STRING("0", check(reg, "L").c_str());

  g_now = 1100;
  host.tick(ctx, "L");
  TEST_ASSERT_EQUAL_STRING("1+", check(reg, "L").c_str());

  g_now = 1200;
  host.tick(ctx, "L");
  TEST_ASSERT_EQUAL_STRING("1+", check(reg, "L").c_str());

  g_now = 2300;
  host.tick(ctx, "Time");
  TEST_ASSERT_EQUAL_STRING("2+-", check(reg, "L").c_str());

  TEST_ASSERT_TRUE(host.errorOf("L").empty());
}

static const char* kCounter =
    "var n\ndef init() self.n = 0 end\n"
    "def loop() self.n += 1 end\ndef draw() end\n"
    "def check() return str(self.n) end";

static void test_loop_runs_for_offscreen_scripts() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("A", app(kCounter));
  host.set("B", app(kCounter));
  RenderCtx ctx;
  g_now = 1000;
  host.tick(ctx, "A");
  TEST_ASSERT_EQUAL_STRING("1", check(reg, "A").c_str());
  TEST_ASSERT_EQUAL_STRING("1", check(reg, "B").c_str());
}

static void test_stagger_spreads_first_loops_across_seconds() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("A", app(kCounter));
  host.set("B", app(kCounter));
  host.set("C", app(kCounter));
  host.staggerFirstLoops(2000);
  RenderCtx ctx;

  g_now = 1000;
  host.tick(ctx, "A");
  TEST_ASSERT_EQUAL_STRING("1", check(reg, "A").c_str());
  TEST_ASSERT_EQUAL_STRING("0", check(reg, "B").c_str());
  TEST_ASSERT_EQUAL_STRING("0", check(reg, "C").c_str());

  g_now = 2000;
  host.tick(ctx, "A");
  TEST_ASSERT_EQUAL_STRING("2", check(reg, "A").c_str());
  TEST_ASSERT_EQUAL_STRING("1", check(reg, "B").c_str());
  TEST_ASSERT_EQUAL_STRING("0", check(reg, "C").c_str());

  g_now = 4200;
  host.tick(ctx, "A");
  TEST_ASSERT_EQUAL_STRING("3", check(reg, "A").c_str());
  TEST_ASSERT_EQUAL_STRING("2", check(reg, "B").c_str());
  TEST_ASSERT_EQUAL_STRING("1", check(reg, "C").c_str());
}

static void test_reinstall_clears_the_stagger_hold() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("A", app(kCounter));
  host.set("B", app(kCounter));
  host.staggerFirstLoops(60000);
  host.set("B", app(kCounter));
  RenderCtx ctx;
  g_now = 1000;
  host.tick(ctx, "A");
  TEST_ASSERT_EQUAL_STRING("1", check(reg, "B").c_str());
}

static void test_tls_boot_grace_window_ends() {
  TEST_ASSERT_TRUE(script::tlsBootGraceActive(0));
  TEST_ASSERT_TRUE(script::tlsBootGraceActive(script::kTlsBootGraceMs - 1));
  TEST_ASSERT_FALSE(script::tlsBootGraceActive(script::kTlsBootGraceMs));
  TEST_ASSERT_FALSE(script::tlsBootGraceActive(script::kTlsBootGraceMs + 1));
}

static void test_tight_fetch_retries_are_bounded() {
  TEST_ASSERT_TRUE(script::shouldRetryTightFetch(0));
  TEST_ASSERT_TRUE(script::shouldRetryTightFetch(script::kMaxTightRetries - 1));
  TEST_ASSERT_FALSE(script::shouldRetryTightFetch(script::kMaxTightRetries));
}

static void test_every_script_is_active_before_a_running_list_arrives() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("A", app(kCounter));
  TEST_ASSERT_TRUE(host.active("A"));
}

static void test_a_script_left_out_of_the_running_list_stops() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("A", app(kCounter));
  host.set("B", app(kCounter));
  host.setRunningScripts({"Time", "A"});
  TEST_ASSERT_TRUE(host.active("A"));
  TEST_ASSERT_FALSE(host.active("B"));

  RenderCtx ctx;
  g_now = 1000;
  host.tick(ctx, "A");
  TEST_ASSERT_EQUAL_STRING("1", check(reg, "A").c_str());
  TEST_ASSERT_EQUAL_STRING("0", check(reg, "B").c_str());
}

static void test_the_headless_flag_does_not_keep_a_script_running() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("A", app(kCounter));
  host.set("B", "# @headless true\n" + app(kCounter));

  host.setRunningScripts({"A"});
  TEST_ASSERT_FALSE(host.active("B"));
  RenderCtx ctx;
  g_now = 1000;
  host.tick(ctx, "A");
  TEST_ASSERT_EQUAL_STRING("0", check(reg, "B").c_str());

  host.setRunningScripts({"A", "B"});
  g_now = 2000;
  host.tick(ctx, "A");
  TEST_ASSERT_EQUAL_STRING("1", check(reg, "B").c_str());

  TEST_ASSERT_TRUE(host.isHeadless("B"));
  TEST_ASSERT_FALSE(host.isHeadless("A"));
  TEST_ASSERT_TRUE(host.list().at("B").headless);
  TEST_ASSERT_FALSE(host.list().at("A").headless);
}

static void test_a_headless_script_needs_no_draw_method() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);

  TEST_ASSERT_TRUE(host.set("Listener", "# @headless true\ndef setup() end\nclass L\n"
                                        "  def setup() end\nend\nreturn L()"));
  TEST_ASSERT_TRUE(host.errorOf("Listener").empty());

  TEST_ASSERT_TRUE(host.set("Drawer", "class D\n  def setup() end\nend\nreturn D()"));
  TEST_ASSERT_EQUAL_STRING("no draw() method", host.errorOf("Drawer").message.c_str());
}

static void test_the_headless_flag_follows_a_re_saved_header() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("A", app(kCounter));
  TEST_ASSERT_FALSE(host.isHeadless("A"));

  host.set("A", "# @headless true\n" + app(kCounter));
  TEST_ASSERT_TRUE(host.isHeadless("A"));

  host.set("A", app(kCounter));
  TEST_ASSERT_FALSE(host.isHeadless("A"));
}

static void test_rejoining_the_running_list_starts_a_script_again() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("A", app(kCounter));
  host.setRunningScripts({"Time"});
  RenderCtx ctx;
  g_now = 1000;
  host.tick(ctx, "Time");
  TEST_ASSERT_EQUAL_STRING("0", check(reg, "A").c_str());

  host.setRunningScripts({"Time", "A"});
  g_now = 2000;
  host.tick(ctx, "Time");
  TEST_ASSERT_EQUAL_STRING("1", check(reg, "A").c_str());
}

static void test_http_answers_do_not_reach_an_inactive_script() {
  FakeHttp fake;
  g_svc.http = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var t\ndef init() self.t = -1 end\n"
                    "def setup() http.get('http://x/', def(b) self.t = int(b) end) end\n"
                    "def draw() end\ndef check() return str(self.t) end"));
  host.setRunningScripts({"Time"});

  host.pushHttpResult({fake.last(), true, 200, "33"});
  RenderCtx ctx;
  host.tick(ctx, "Time");
  TEST_ASSERT_EQUAL_STRING("-1", check(reg, "W").c_str());
}

static void test_mqtt_messages_do_not_reach_an_inactive_script() {
  FakeMqtt fake;
  g_svc.mqtt = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("M", app("var t\ndef init() self.t = '' end\n"
                    "def setup() mqtt.subscribe('a/b', def(t, p) self.t = p end) end\n"
                    "def draw() end\ndef check() return self.t end"));
  host.setRunningScripts({"Time"});

  host.pushMqttMessage({"a/b", "hi", "a/b"});
  RenderCtx ctx;
  host.tick(ctx, "Time");
  TEST_ASSERT_EQUAL_STRING("", check(reg, "M").c_str());
}

static void test_button_goes_only_to_the_visible_script() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  const std::string src = app("var t\ndef init() self.t = '' end\ndef draw() end\n"
                              "def on_button(b) self.t += b end\ndef check() return self.t end");
  host.set("A", src);
  host.set("B", src);
  RenderCtx ctx;
  host.tick(ctx, "A");
  host.handleButton("A", "select");
  host.handleButton("B", "left");
  host.handleButton("Time", "right");
  TEST_ASSERT_EQUAL_STRING("select", check(reg, "A").c_str());
  TEST_ASSERT_EQUAL_STRING("", check(reg, "B").c_str());
}


static void test_http_result_routed_to_owning_script() {
  FakeHttp fake;
  g_svc.http = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var t\ndef init() self.t = -1 end\n"
                    "def setup() http.get('http://x/', def(b) self.t = int(b) end) end\n"
                    "def draw() pixel(0, 0, self.t) end"));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)fake.ids.size());

  host.pushHttpResult({fake.last(), true, 200, "33"});
  RenderCtx ctx;
  host.tick(ctx, "W");
  Canvas c(32, 8);
  reg.find("W")->render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(33u, c.getPixel(0, 0));
}

static void test_http_ids_are_unique_across_scripts() {
  FakeHttp fake;
  g_svc.http = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  const std::string src = app("var t\ndef init() self.t = -1 end\n"
                              "def setup() http.get('http://x/', def(b) self.t = int(b) end) end\n"
                              "def draw() pixel(0, 0, self.t) end");
  host.set("A", src);
  host.set("B", src);
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)fake.ids.size());
  TEST_ASSERT_TRUE(fake.ids[0] != fake.ids[1]);

  host.pushHttpResult({fake.ids[0], true, 200, "11"});
  host.pushHttpResult({fake.ids[1], true, 200, "22"});
  RenderCtx ctx;
  host.tick(ctx, "A");
  Canvas c(32, 8);
  reg.find("A")->render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(11u, c.getPixel(0, 0));
  reg.find("B")->render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(22u, c.getPixel(0, 0));
}

static void test_http_failure_and_unknown_id() {
  FakeHttp fake;
  g_svc.http = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var t\ndef init() self.t = -1 end\n"
                    "def setup() http.get('http://x/', def(b) self.t = b == nil ? 7 : 1 end) end\n"
                    "def draw() pixel(0, 0, self.t) end"));
  host.pushHttpResult({9999, true, 200, "x"});
  host.pushHttpResult({fake.last(), false, 0, ""});
  RenderCtx ctx;
  host.tick(ctx, "W");
  Canvas c(32, 8);
  reg.find("W")->render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(7u, c.getPixel(0, 0));
  TEST_ASSERT_TRUE(host.errorOf("W").empty());
}

static void test_http_result_for_replaced_script_is_dropped() {
  FakeHttp fake;
  g_svc.http = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var t\ndef init() self.t = -1 end\n"
                    "def setup() http.get('http://x/', def(b) self.t = 1 end) end\n"
                    "def draw() end"));
  const uint32_t inflight = fake.last();
  host.set("W", app("var t\ndef init() self.t = -1 end\n"
                    "def draw() pixel(0, 0, self.t == -1 ? 0x44 : 0) end"));
  host.pushHttpResult({inflight, true, 200, "1"});
  RenderCtx ctx;
  host.tick(ctx, "W");
  Canvas c(32, 8);
  reg.find("W")->render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0x44u, c.getPixel(0, 0));
}

static void test_http_pending_cap_per_script() {
  FakeHttp fake;
  g_svc.http = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var fails\ndef init() self.fails = 0 end\n"
                    "def setup()\n"
                    "  for i : 0 .. 11\n"
                    "    http.get('http://x/', def(b) if b == nil self.fails += 1 end end)\n"
                    "  end\n"
                    "end\n"
                    "def draw() end\n"
                    "def check() return str(self.fails) end"));
  TEST_ASSERT_TRUE(host.errorOf("W").empty());
  TEST_ASSERT_EQUAL_UINT((unsigned)script::kMaxPendingHttp, (unsigned)fake.ids.size());
  TEST_ASSERT_EQUAL_STRING("4", check(reg, "W").c_str());
}

static void test_http_without_transport_soft_fails() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var t\ndef init() self.t = -1 end\n"
                    "def setup() http.get('http://x/', def(b) self.t = b == nil ? 5 : 1 end) end\n"
                    "def draw() pixel(0, 0, self.t) end"));
  TEST_ASSERT_TRUE(host.errorOf("W").empty());
  Canvas c(32, 8);
  RenderCtx ctx;
  reg.find("W")->render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(5u, c.getPixel(0, 0));
}

static void test_unanswered_http_requests_free_their_slots() {
  FakeHttp fake;
  g_svc.http = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var fails\ndef init() self.fails = 0 end\n"
                    "def fire()\n"
                    "  for i : 0 .. 7 http.get('http://x/', "
                    "def(b) if b == nil self.fails += 1 end end) end\n"
                    "end\n"
                    "def setup() self.fire() end\n"
                    "def on_button(b) self.fire() end\n"
                    "def draw() end\n"
                    "def check() return str(self.fails) .. '/' .. str(size(_http_cbs)) end"));
  TEST_ASSERT_EQUAL_UINT((unsigned)script::kMaxPendingHttp, (unsigned)fake.ids.size());

  RenderCtx ctx;
  host.tick(ctx, "W");
  TEST_ASSERT_EQUAL_STRING("0/8", check(reg, "W").c_str());

  host.handleButton("W", "select");
  TEST_ASSERT_EQUAL_UINT((unsigned)script::kMaxPendingHttp, (unsigned)fake.ids.size());
  TEST_ASSERT_EQUAL_STRING("8/8", check(reg, "W").c_str());

  g_now = script::kHttpTimeoutMs + 1;
  host.tick(ctx, "W");
  TEST_ASSERT_EQUAL_STRING("16/0", check(reg, "W").c_str());

  host.handleButton("W", "select");
  TEST_ASSERT_EQUAL_UINT((unsigned)(2 * script::kMaxPendingHttp), (unsigned)fake.ids.size());
  TEST_ASSERT_EQUAL_STRING("16/8", check(reg, "W").c_str());
}

static void test_answered_http_request_is_not_swept() {
  FakeHttp fake;
  g_svc.http = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var t\ndef init() self.t = -1 end\n"
                    "def setup() http.get('http://x/', def(b) self.t = b == nil ? 99 : int(b) end) end\n"
                    "def draw() end\n"
                    "def check() return str(self.t) end"));
  host.pushHttpResult({fake.last(), true, 200, "33"});
  RenderCtx ctx;
  g_now = 10;
  host.tick(ctx, "W");
  TEST_ASSERT_EQUAL_STRING("33", check(reg, "W").c_str());

  g_now = script::kHttpTimeoutMs * 4;
  host.tick(ctx, "W");
  TEST_ASSERT_EQUAL_STRING("33", check(reg, "W").c_str());
}


static void test_mqtt_fans_out_to_every_subscriber() {
  FakeMqtt fake;
  g_svc.mqtt = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  const std::string src = app("var t\ndef init() self.t = '' end\n"
                              "def setup() mqtt.subscribe('a/b', "
                              "def(topic, payload) self.t += payload end) end\n"
                              "def draw() end\n"
                              "def check() return self.t end");
  host.set("A", src);
  host.set("B", src);
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)fake.subscribed.size());

  host.pushMqttMessage({"a/b", "hi"});
  host.pushMqttMessage({"other", "no"});
  RenderCtx ctx;
  host.tick(ctx, "A");
  TEST_ASSERT_EQUAL_STRING("hi", check(reg, "A").c_str());
  TEST_ASSERT_EQUAL_STRING("hi", check(reg, "B").c_str());
}

static void test_mqtt_publish_reaches_transport() {
  FakeMqtt fake;
  g_svc.mqtt = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("P", app("def setup() mqtt.publish('a/b', 'v') end\ndef draw() end"));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)fake.published.size());
  TEST_ASSERT_EQUAL_STRING("a/b=v", fake.published[0].c_str());
}

static void test_remove_drops_subscriptions() {
  FakeMqtt fake;
  g_svc.mqtt = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  const std::string src = app("def setup() mqtt.subscribe('a/b', def(t, p) end) end\ndef draw() end");
  host.set("A", src);
  host.set("B", src);
  host.remove("A");
  TEST_ASSERT_EQUAL_UINT(0u, (unsigned)fake.unsubscribed.size());
  host.remove("B");
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)fake.unsubscribed.size());

  host.pushMqttMessage({"a/b", "hi"});
  RenderCtx ctx;
  host.tick(ctx, "X");
  TEST_ASSERT_EQUAL_UINT(0u, (unsigned)host.count());
}

static void test_replace_releases_slots_and_broker_subscriptions() {
  FakeHttp fhttp;
  FakeMqtt fmqtt;
  g_svc.http = &fhttp;
  g_svc.mqtt = &fmqtt;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  const std::string src = app("def setup()\n"
                              "  mqtt.subscribe('a/b', def(t, p) end)\n"
                              "  for i : 0 .. 7 http.get('http://x/', def(b) end) end\n"
                              "end\n"
                              "def draw() end");

  host.set("W", src);
  TEST_ASSERT_EQUAL_UINT((unsigned)script::kMaxPendingHttp, (unsigned)fhttp.ids.size());
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)fmqtt.subscribed.size());

  host.set("W", src);

  TEST_ASSERT_EQUAL_UINT((unsigned)(2 * script::kMaxPendingHttp),
                         (unsigned)fhttp.ids.size());
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)fmqtt.unsubscribed.size());
  TEST_ASSERT_EQUAL_STRING("a/b", fmqtt.unsubscribed[0].c_str());
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)fmqtt.subscribed.size());
}

static void test_mqtt_wildcard_delivers_the_concrete_topic() {
  FakeMqtt fake;
  g_svc.mqtt = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var seen\ndef init() self.seen = '' end\n"
                    "def setup() mqtt.subscribe('sensor/#', "
                    "def(topic, payload) self.seen = topic end) end\n"
                    "def draw() end\n"
                    "def check() return self.seen end"));

  script::MqttMessage m;
  m.topic = "sensor/kitchen/temp";
  m.payload = "21.5";
  m.filter = "sensor/#";
  host.pushMqttMessage(m);

  RenderCtx ctx;
  host.tick(ctx, "W");
  TEST_ASSERT_EQUAL_STRING("sensor/kitchen/temp", check(reg, "W").c_str());
}

static void test_mqtt_empty_filter_routes_by_topic() {
  FakeMqtt fake;
  g_svc.mqtt = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var seen\ndef init() self.seen = '' end\n"
                    "def setup() mqtt.subscribe('a/b', def(topic, payload) self.seen = topic end) end\n"
                    "def draw() end\n"
                    "def check() return self.seen end"));
  host.pushMqttMessage({"a/b", "hi"});
  RenderCtx ctx;
  host.tick(ctx, "W");
  TEST_ASSERT_EQUAL_STRING("a/b", check(reg, "W").c_str());
}


static void test_broken_script_does_not_receive_http_results() {
  FakeHttp fake;
  g_svc.http = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var got, nil_fn\ndef init() self.got = 0 self.nil_fn = nil end\n"
                    "def setup() http.get('http://x/', def(b) self.got += 1 end) end\n"
                    "def draw() self.nil_fn() end\n"
                    "def check() return str(self.got) end"));
  const uint32_t inflight = fake.last();

  Canvas c(32, 8);
  RenderCtx ctx;
  reg.find("W")->render(c, ctx);
  TEST_ASSERT_FALSE(host.errorOf("W").empty());

  host.pushHttpResult({inflight, true, 200, "1"});
  host.tick(ctx, "W");
  TEST_ASSERT_EQUAL_STRING("0", check(reg, "W").c_str());
}

static void test_broken_script_does_not_receive_mqtt_messages() {
  FakeMqtt fake;
  g_svc.mqtt = &fake;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var got, nil_fn\ndef init() self.got = 0 self.nil_fn = nil end\n"
                    "def setup() mqtt.subscribe('a/b', def(t, p) self.got += 1 end) end\n"
                    "def draw() self.nil_fn() end\n"
                    "def check() return str(self.got) end"));

  Canvas c(32, 8);
  RenderCtx ctx;
  reg.find("W")->render(c, ctx);
  TEST_ASSERT_FALSE(host.errorOf("W").empty());

  host.pushMqttMessage({"a/b", "hi"});
  host.tick(ctx, "W");
  TEST_ASSERT_EQUAL_STRING("0", check(reg, "W").c_str());
}


static void test_wants_show_asks_the_named_app() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("Yes", app("def draw() end\ndef should_show() return true end"));
  host.set("No", app("def draw() end\ndef should_show() return false end"));

  TEST_ASSERT_TRUE(host.wantsShow("Yes"));
  TEST_ASSERT_FALSE(host.wantsShow("No"));
  TEST_ASSERT_TRUE(host.wantsShow("Time"));
  host.remove("No");
  TEST_ASSERT_TRUE(host.wantsShow("No"));
}

static void test_wants_show_is_reported_in_the_listing() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("Q", app("var on\ndef init() self.on = false end\n"
                    "def draw() end\n"
                    "def should_show() return self.on end\n"
                    "def loop() self.on = true end"));

  TEST_ASSERT_FALSE(host.list()["Q"].skipping);
  TEST_ASSERT_FALSE(host.wantsShow("Q"));
  TEST_ASSERT_TRUE(host.list()["Q"].skipping);

  RenderCtx ctx;
  g_now = 1000;
  host.tick(ctx, "");
  TEST_ASSERT_TRUE(host.wantsShow("Q"));
  TEST_ASSERT_FALSE(host.list()["Q"].skipping);
}

static void test_duration_is_resolved_when_the_app_is_shown() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("D", app("def draw() end\ndef duration() return 3000 end"));

  RenderCtx ctx;
  TEST_ASSERT_EQUAL_INT(0, (int)host.durationMs("D"));

  g_now = 1000;
  host.tick(ctx, "D");
  TEST_ASSERT_EQUAL_INT(3000, (int)host.durationMs("D"));

  TEST_ASSERT_EQUAL_INT(0, (int)host.durationMs("Time"));
}

static void test_duration_reevaluates_on_each_show() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("D", app("def draw() end\ndef duration() return hour() * 1000 end"));

  RenderCtx ctx;
  ctx.hour = 4;
  g_now = 1000;
  host.tick(ctx, "D");
  TEST_ASSERT_EQUAL_INT(4000, (int)host.durationMs("D"));

  g_now = 2000;
  host.tick(ctx, "Time");
  ctx.hour = 9;
  g_now = 3000;
  host.tick(ctx, "D");
  TEST_ASSERT_EQUAL_INT(9000, (int)host.durationMs("D"));
}

static void test_duration_without_the_hook_or_non_positive_is_global() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("None", app("def draw() end"));
  host.set("Zero", app("def draw() end\ndef duration() return 0 end"));
  host.set("Neg", app("def draw() end\ndef duration() return -5 end"));
  host.set("Nil", app("def draw() end\ndef duration() end"));

  RenderCtx ctx;
  g_now = 1000;
  host.tick(ctx, "None");
  host.tick(ctx, "Zero");
  host.tick(ctx, "Neg");
  host.tick(ctx, "Nil");
  TEST_ASSERT_EQUAL_INT(0, (int)host.durationMs("None"));
  TEST_ASSERT_EQUAL_INT(0, (int)host.durationMs("Zero"));
  TEST_ASSERT_EQUAL_INT(0, (int)host.durationMs("Neg"));
  TEST_ASSERT_EQUAL_INT(0, (int)host.durationMs("Nil"));
}

static void test_store_write_from_should_show_reaches_the_sink() {
  FakeSink sink;
  g_svc.storeSink = &sink;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("def draw() end\ndef should_show() store.set('asked', 1) return false end"));
  TEST_ASSERT_EQUAL_UINT(0u, (unsigned)sink.writes.size());
  TEST_ASSERT_FALSE(host.wantsShow("W"));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)sink.writes.size());
  TEST_ASSERT_TRUE(sink.writes[0].find("\"asked\"") != std::string::npos);
}

// Only one store flush is pending at a time, so the app being shown must not be able to
// overwrite what the app being hidden wrote in the same pass.
static void test_both_sides_of_a_switch_reach_the_sink() {
  FakeSink sink;
  g_svc.storeSink = &sink;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("A", app("def draw() end\ndef on_hide() store.set('gone', 1) end"));
  host.set("B", app("def draw() end\ndef on_show() store.set('here', 1) end"));

  RenderCtx ctx;
  host.tick(ctx, "A");
  sink.writes.clear();
  host.tick(ctx, "B");

  bool gone = false, here = false;
  for (const std::string& w : sink.writes) {
    if (w.find("\"gone\"") != std::string::npos) gone = true;
    if (w.find("\"here\"") != std::string::npos) here = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(gone, "the hidden app's store write was lost");
  TEST_ASSERT_TRUE_MESSAGE(here, "the shown app's store write was lost");
}

static void test_store_writes_routed_to_sink() {
  FakeSink sink;
  g_svc.storeSink = &sink;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("S", app("var n\ndef init() self.n = 0 end\n"
                    "def setup() store.set('a', 1) end\n"
                    "def loop() self.n += 1 store.set('n', self.n) end\n"
                    "def draw() end"));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)sink.writes.size());
  TEST_ASSERT_TRUE(sink.writes[0].rfind("S:", 0) == 0);

  RenderCtx ctx;
  g_now = 1000;
  host.tick(ctx, "S");
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)sink.writes.size());
  TEST_ASSERT_TRUE(sink.writes[1].find("\"n\"") != std::string::npos);
}

static void test_store_write_from_draw_is_routed_on_next_tick() {
  FakeSink sink;
  g_svc.storeSink = &sink;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("D", app("def draw() store.set('d', 1) end"));
  TEST_ASSERT_EQUAL_UINT(0u, (unsigned)sink.writes.size());
  Canvas c(32, 8);
  RenderCtx ctx;
  reg.find("D")->render(c, ctx);
  host.tick(ctx, "D");
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)sink.writes.size());
  TEST_ASSERT_TRUE(sink.writes[0].rfind("D:", 0) == 0);
}

static void test_store_write_is_attributed_to_the_writing_script() {
  FakeSink sink;
  g_svc.storeSink = &sink;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);

  host.set("A", app("def draw() store.set('k', 'A_SECRET') end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  reg.find("A")->render(c, ctx);

  host.set("B", app("def setup() end\ndef draw() end"));
  host.tick(ctx, "B");

  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)sink.writes.size());
  TEST_ASSERT_TRUE_MESSAGE(sink.writes[0].rfind("A:", 0) == 0,
                           sink.writes[0].c_str());
  TEST_ASSERT_TRUE(sink.writes[0].find("A_SECRET") != std::string::npos);
}

static void test_store_is_isolated_per_app() {
  FakeSink sink;
  g_svc.storeSink = &sink;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("A", app("def setup() store.set('x', 1) end\ndef draw() end\n"
                    "def check() return str(store.get('x', -1)) end"));
  host.set("B", app("def setup() store.set('x', 2) end\ndef draw() end\n"
                    "def check() return str(store.get('x', -1)) end"));
  TEST_ASSERT_EQUAL_STRING("1", check(reg, "A").c_str());
  TEST_ASSERT_EQUAL_STRING("2", check(reg, "B").c_str());
}


static void test_declared_defaults_reach_the_store() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W",
           "# @config city text default=\"Berlin\"\n"
           "# @config every number default=15\n"
           "# @config tint color default=#FF8800\n" +
               app("def draw() end\n"
                   "def check() return store.get('city')+'/'+str(store.get('every'))+'/'+"
                   "str(store.get('tint')) end"));
  TEST_ASSERT_EQUAL_STRING("Berlin/15/16746496", check(reg, "W").c_str());
}

static void test_a_stored_value_wins_over_the_declared_default() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W",
           "# @config city text default=\"Berlin\"\n"
           "# @config every number default=15\n" +
               app("def draw() end\n"
                   "def check() return store.get('city')+'/'+str(store.get('every')) end"),
           "{\"city\":\"Rom\"}");
  TEST_ASSERT_EQUAL_STRING("Rom/15", check(reg, "W").c_str());
}

static void test_a_script_without_config_keeps_its_store_untouched() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("P", app("def draw() end\ndef check() return str(store.get('x', -1)) end"),
           "{\"x\":5}");
  TEST_ASSERT_EQUAL_STRING("5", check(reg, "P").c_str());
}

static void test_saving_settings_restarts_the_script_with_the_new_values() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  const std::string src =
      "# @config city text default=\"Berlin\"\n" +
      app("def draw() end\ndef check() return store.get('city') end");
  host.set("W", src);
  TEST_ASSERT_EQUAL_STRING("Berlin", check(reg, "W").c_str());

  const script::ConfigPatch p =
      script::applyConfigPatch(script::parseConfig(src), "{}", "{\"city\":\"Hamburg\"}");
  TEST_ASSERT_TRUE(p.ok);
  host.set("W", src, p.storeJson);
  TEST_ASSERT_EQUAL_STRING("Hamburg", check(reg, "W").c_str());
}

static void test_the_apps_list_reports_whether_a_script_has_settings() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", "# @config city text\n" + app("def draw() end"));
  host.set("P", app("def draw() end"));
  const auto list = host.list();
  TEST_ASSERT_TRUE(list.at("W").config);
  TEST_ASSERT_FALSE(list.at("P").config);
}

static void test_wall_clock_reaches_every_callback() {
  FakeHttp http;
  g_svc.http = &http;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("W", app("var v_show, v_hide, v_btn, v_http, v_mqtt, v_loop\n"
                    "def init()\n"
                    "  self.v_show = -9 self.v_hide = -9 self.v_btn = -9\n"
                    "  self.v_http = -9 self.v_mqtt = -9 self.v_loop = -9\n"
                    "end\n"
                    "def setup()\n"
                    "  mqtt.subscribe('t', def(tp, pl) self.v_mqtt = hour() end)\n"
                    "  http.get('http://x', def(b) self.v_http = hour() end)\n"
                    "end\n"
                    "def on_show() self.v_show = hour() end\n"
                    "def on_hide() self.v_hide = hour() end\n"
                    "def on_button(b) self.v_btn = hour() end\n"
                    "def loop() self.v_loop = hour() end\n"
                    "def draw() end\n"
                    "def check() return str(self.v_show)+','+str(self.v_hide)+','+str(self.v_btn)+','+"
                    "str(self.v_http)+','+str(self.v_mqtt)+','+str(self.v_loop) end"));

  RenderCtx ctx;
  ctx.hour = 13;

  g_now = 1100;
  host.tick(ctx, "W");
  host.handleButton("W", "select");

  host.pushHttpResult({http.last(), true, 200, "body"});
  host.pushMqttMessage({"t", "p", "t"});
  g_now = 2200;
  host.tick(ctx, "W");

  g_now = 3300;
  host.tick(ctx, "Time");

  TEST_ASSERT_TRUE(host.errorOf("W").empty());
  TEST_ASSERT_EQUAL_STRING("13,13,13,13,13,13", check(reg, "W").c_str());
}

static void test_setup_sees_the_last_known_clock() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  const std::string src = app("var v\ndef init() self.v = -9 end\n"
                              "def setup() self.v = hour() end\ndef draw() end\n"
                              "def check() return str(self.v) end");

  host.set("S", src);
  TEST_ASSERT_EQUAL_STRING("-1", check(reg, "S").c_str());

  RenderCtx ctx;
  ctx.hour = 21;
  host.tick(ctx, "Time");
  host.set("S", src);
  TEST_ASSERT_EQUAL_STRING("21", check(reg, "S").c_str());
}


namespace {

std::string bystander() {
  return app("var n\ndef init() self.n = 0 end\n"
             "def setup() self.n += 1 end\n"
             "def draw() self.n += 1 end\n"
             "def loop() self.n += 1 end\n"
             "def on_show() self.n += 1 end\n"
             "def on_button(b) self.n += 1 end\n"
             "def check() return str(self.n) end");
}

void assertSpun(script::ScriptHost& host, const char* name) {
  const std::string err = host.errorOf(name).message;
  TEST_ASSERT_TRUE_MESSAGE(err.find("instruction limit exceeded") != std::string::npos,
                           err.c_str());
}

}

static void test_runaway_draw_latches_and_spares_the_rest() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("Spin", app("def draw() while true end end"));
  host.set("OK", bystander());

  Canvas c(32, 8);
  RenderCtx ctx;
  reg.find("Spin")->render(c, ctx);
  assertSpun(host, "Spin");

  g_now = 1100;
  host.tick(ctx, "OK");
  reg.find("OK")->render(c, ctx);
  TEST_ASSERT_TRUE(host.errorOf("OK").empty());
  TEST_ASSERT_EQUAL_STRING("4", check(reg, "OK").c_str());
}

static void test_runaway_loop_latches_and_spares_the_rest() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("Spin", app("def draw() end\ndef loop() while true end end"));
  host.set("OK", bystander());

  RenderCtx ctx;
  g_now = 1100;
  host.tick(ctx, "Time");
  assertSpun(host, "Spin");
  TEST_ASSERT_TRUE(host.errorOf("OK").empty());
  TEST_ASSERT_EQUAL_STRING("2", check(reg, "OK").c_str());

  g_now = 2200;
  host.tick(ctx, "Time");
  TEST_ASSERT_EQUAL_STRING("3", check(reg, "OK").c_str());
}

static void test_runaway_setup_latches_at_install() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("OK", bystander());
  TEST_ASSERT_TRUE(host.set("Spin", app("def setup() while true end end\ndef draw() end")));
  assertSpun(host, "Spin");
  TEST_ASSERT_NOT_NULL(reg.find("Spin"));

  RenderCtx ctx;
  g_now = 1100;
  host.tick(ctx, "OK");
  TEST_ASSERT_TRUE(host.errorOf("OK").empty());
  TEST_ASSERT_EQUAL_STRING("3", check(reg, "OK").c_str());
}

static void test_runaway_on_button_latches_and_spares_the_rest() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("Spin", app("def draw() end\ndef on_button(b) while true end end"));
  host.set("OK", bystander());

  RenderCtx ctx;
  g_now = 1100;
  host.tick(ctx, "Spin");
  host.handleButton("Spin", "select");
  assertSpun(host, "Spin");

  g_now = 2200;
  host.tick(ctx, "OK");
  host.handleButton("OK", "select");
  TEST_ASSERT_TRUE(host.errorOf("OK").empty());
  TEST_ASSERT_EQUAL_STRING("5", check(reg, "OK").c_str());
}

static void test_runaway_http_callback_latches_and_spares_the_rest() {
  FakeHttp http;
  g_svc.http = &http;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("Spin", app("def draw() end\n"
                       "def setup() http.get('http://x', def(b) while true end end) end"));
  host.set("OK", bystander());
  TEST_ASSERT_TRUE(host.errorOf("Spin").empty());

  RenderCtx ctx;
  host.pushHttpResult({http.last(), true, 200, "body"});
  g_now = 1100;
  host.tick(ctx, "OK");
  assertSpun(host, "Spin");
  TEST_ASSERT_TRUE(host.errorOf("OK").empty());
  TEST_ASSERT_EQUAL_STRING("3", check(reg, "OK").c_str());
}


static void test_list_reports_meta_and_errors() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("ok",
           "# @name Nice Clock\n# @desc Shows time\n# @author me\n# @version 1.2\n" +
               app("def draw() end"));
  host.set("bad", app("def draw( end"));
  auto l = host.list();
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)l.size());
  TEST_ASSERT_EQUAL_STRING("Nice Clock", l["ok"].metaName.c_str());
  TEST_ASSERT_EQUAL_STRING("Shows time", l["ok"].desc.c_str());
  TEST_ASSERT_EQUAL_STRING("me", l["ok"].author.c_str());
  TEST_ASSERT_EQUAL_STRING("1.2", l["ok"].version.c_str());
  TEST_ASSERT_TRUE(l["ok"].error.empty());
  TEST_ASSERT_TRUE(l["bad"].error.message.find("syntax_error") != std::string::npos);
}

static void test_destructor_unregisters_apps() {
  AppRegistry reg;
  {
    script::ScriptHost host(reg, g_svc, nullptr, nullptr);
    host.set("A", app("def draw() end"));
    TEST_ASSERT_NOT_NULL(reg.find("A"));
  }
  TEST_ASSERT_NULL(reg.find("A"));
}


static void test_service_installs_and_persists() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  std::vector<std::string> saved, removed;
  script::ScriptService svc(
      host, [&](const std::string& n, const std::string& s) { saved.push_back(n + "=" + s); },
      [&](const std::string& n) { removed.push_back(n); });

  const std::string src = app("def draw() end");
  DispatchDetail d;
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("A", src, d));
  TEST_ASSERT_EQUAL_STRING("", d.message.c_str());
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)saved.size());
  TEST_ASSERT_EQUAL_STRING(("A=" + src).c_str(), saved[0].c_str());
  TEST_ASSERT_NOT_NULL(reg.find("A"));

  svc.removeScript("A");
  TEST_ASSERT_NULL(reg.find("A"));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)removed.size());
}

static void test_service_stores_a_script_that_does_not_compile() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  std::vector<std::string> saved;
  script::ScriptService svc(
      host, [&](const std::string& n, const std::string&) { saved.push_back(n); }, nullptr);

  DispatchDetail d;
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("bad", app("def draw( end"), d));
  TEST_ASSERT_TRUE(d.message.find("syntax_error") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)saved.size());
}

static void test_service_refused_for_low_memory_is_capacity_and_stores_nothing() {
  AppRegistry reg;
  std::size_t freeHeap = 1024 * 1024;
  g_svc.freeHeap = [&freeHeap] { return freeHeap; };
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  std::vector<std::string> saved;
  script::ScriptService svc(
      host, [&](const std::string& n, const std::string&) { saved.push_back(n); }, nullptr);

  DispatchDetail d;
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("A", app("def draw() end"), d));
  d.clear();

  const std::string src = app("def draw() end");
  freeHeap = script::installNeedsBytes(src.size()) - 1;
  TEST_ASSERT_EQUAL(DispatchResult::Capacity, svc.setScript("B", src, d));
  TEST_ASSERT_EQUAL_STRING("name", d.field.c_str());
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)saved.size());
  TEST_ASSERT_NULL(reg.find("B"));

  freeHeap = 1024 * 1024;
  d.clear();
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("A", app("def draw() end"), d));
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)saved.size());
}

static void test_service_resave_preserves_the_store() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  std::map<std::string, std::string> persisted{{"S", "{\"n\":7}"}};
  g_svc.readStore = [&](const std::string& n, std::string& out) {
    auto it = persisted.find(n);
    if (it == persisted.end()) return false;
    out = it->second;
    return true;
  };
  script::ScriptService svc(host, nullptr, nullptr);

  const std::string src = app("def draw() end\ndef check() return str(store.get('n', -1)) end");
  DispatchDetail d;
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("S", src, d));
  TEST_ASSERT_EQUAL_STRING("7", check(reg, "S").c_str());

  d.clear();
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("T", src, d));
  TEST_ASSERT_EQUAL_STRING("-1", check(reg, "T").c_str());
}

static void test_service_remove_then_install_starts_empty() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  std::map<std::string, std::string> persisted{{"S", "{\"n\":7}"}};
  g_svc.readStore = [&](const std::string& n, std::string& out) {
    auto it = persisted.find(n);
    if (it == persisted.end()) return false;
    out = it->second;
    return true;
  };
  script::ScriptService svc(host, nullptr,
                            [&](const std::string& n) { persisted.erase(n); });

  const std::string src = app("def draw() end\ndef check() return str(store.get('n', -1)) end");
  DispatchDetail d;
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("S", src, d));
  TEST_ASSERT_EQUAL_STRING("7", check(reg, "S").c_str());

  svc.removeScript("S");
  d.clear();
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("S", src, d));
  TEST_ASSERT_EQUAL_STRING("-1", check(reg, "S").c_str());
}

static void test_service_drops_a_setting_the_new_source_no_longer_declares() {
  FakeSink sink;
  g_svc.storeSink = &sink;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);

  const std::string body =
      app("def draw() end\n"
          "def check() return str(store.get('city', 'gone'))+'/'+str(store.get('hits', -1)) end");
  const std::string v1 = "# @config city text default=\"Rom\"\n" + body;
  const std::string v2 = "# @config tint color default=#FFFFFF\n" + body;

  std::map<std::string, std::string> sources;
  std::map<std::string, std::string> stores{{"S", "{\"city\":\"Wien\",\"hits\":7}"}};
  g_svc.readSource = [&](const std::string& n, std::string& out) {
    auto it = sources.find(n);
    if (it == sources.end()) return false;
    out = it->second;
    return true;
  };
  g_svc.readStore = [&](const std::string& n, std::string& out) {
    auto it = stores.find(n);
    if (it == stores.end()) return false;
    out = it->second;
    return true;
  };
  script::ScriptService svc(
      host, [&](const std::string& n, const std::string& s) { sources[n] = s; }, nullptr);

  DispatchDetail d;
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("S", v1, d));
  TEST_ASSERT_EQUAL_STRING("Wien/7", check(reg, "S").c_str());

  stores["S"] = "{\"city\":\"Wien\",\"hits\":7}";
  d.clear();
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("S", v2, d));
  TEST_ASSERT_EQUAL_STRING("gone/7", check(reg, "S").c_str());
}

static void test_shared_value_crosses_apps() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("weather", app("def draw() end\ndef setup() shared.set('temp', 12) end"));
  host.set("clock", app("def draw() end\ndef check() return str(shared.get('weather.temp')) end"));
  TEST_ASSERT_EQUAL_STRING("12", check(reg, "clock").c_str());
}

static void test_shared_reads_its_own_namespace_without_a_prefix() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("weather",
           app("def draw() end\ndef setup() shared.set('temp', 12) end\n"
               "def check() return str(shared.get('temp')) end"));
  TEST_ASSERT_EQUAL_STRING("12", check(reg, "weather").c_str());
}

static void test_shared_get_falls_back_to_the_default() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("a", app("def draw() end\ndef check() return str(shared.get('ghost.k', -1)) end"));
  TEST_ASSERT_EQUAL_STRING("-1", check(reg, "a").c_str());
}

static void test_shared_get_without_a_default_is_nil() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("a", app("def draw() end\ndef check() return str(shared.get('ghost.k')) end"));
  TEST_ASSERT_EQUAL_STRING("nil", check(reg, "a").c_str());
}

static void test_shared_keeps_the_value_type() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("p", app("def draw() end\ndef setup()\n"
                    "shared.set('i', 7) shared.set('r', 1.5)\n"
                    "shared.set('b', true) shared.set('s', 'hi')\nend"));
  host.set("c", app("def draw() end\ndef check()\n"
                    "return str(shared.get('p.i')) + ',' + str(shared.get('p.r')) + ',' + "
                    "str(shared.get('p.b')) + ',' + str(shared.get('p.s'))\nend"));
  TEST_ASSERT_EQUAL_STRING("7,1.5,true,hi", check(reg, "c").c_str());
}

static void test_shared_age_counts_from_the_write() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  g_now = 1000;
  host.set("weather", app("def draw() end\ndef setup() shared.set('temp', 12) end"));
  host.set("clock", app("def draw() end\ndef check() return str(shared.age('weather.temp')) end"));
  g_now = 6000;
  TEST_ASSERT_EQUAL_STRING("5000", check(reg, "clock").c_str());
}

static void test_shared_age_of_a_missing_key_is_nil() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("a", app("def draw() end\ndef check() return str(shared.age('ghost.k')) end"));
  TEST_ASSERT_EQUAL_STRING("nil", check(reg, "a").c_str());
}

static void test_shared_set_cannot_reach_another_namespace() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("victim", app("def draw() end\ndef setup() shared.set('k', 1) end\n"
                         "def check() return str(shared.get('k')) end"));
  host.set("attacker", app("def draw() end\ndef check() return str(shared.set('victim.k', 99)) end"));
  TEST_ASSERT_EQUAL_STRING("false", check(reg, "attacker").c_str());
  TEST_ASSERT_EQUAL_STRING("1", check(reg, "victim").c_str());
}

static void test_shared_set_to_nil_erases_the_key() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("p", app("def draw() end\ndef setup() shared.set('k', 1) shared.set('k', nil) end"));
  host.set("c", app("def draw() end\ndef check() return str(shared.get('p.k', -1)) end"));
  TEST_ASSERT_EQUAL_STRING("-1", check(reg, "c").c_str());
}

static void test_shared_rejects_non_scalar_values() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("a", app("def draw() end\ndef check() return str(shared.set('k', [1, 2])) end"));
  TEST_ASSERT_EQUAL_STRING("false", check(reg, "a").c_str());
}

static void test_shared_keys_enumerates_every_provider() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("aa", app("def draw() end\ndef setup() shared.set('temp', 1) end"));
  host.set("bb", app("def draw() end\ndef setup() shared.set('mode', 1) end"));
  host.set("cc", app("def draw() end\ndef check()\n"
                     "  var s = ''\n  for k : shared.keys() s += k + ',' end\n  return s\nend"));
  TEST_ASSERT_EQUAL_STRING("aa.temp,bb.mode,", check(reg, "cc").c_str());
}

static void test_shared_keys_can_be_filtered_by_provider() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("aa", app("def draw() end\ndef setup() shared.set('temp', 1) end"));
  host.set("bb", app("def draw() end\ndef setup() shared.set('mode', 1) end"));
  host.set("cc", app("def draw() end\ndef check()\n"
                     "  var s = ''\n  for k : shared.keys('bb') s += k + ',' end\n  return s\nend"));
  TEST_ASSERT_EQUAL_STRING("bb.mode,", check(reg, "cc").c_str());
}

static void test_removing_a_script_purges_its_shared_keys() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("p", app("def draw() end\ndef setup() shared.set('k', 1) end"));
  host.set("c", app("def draw() end\ndef check() return str(shared.get('p.k', -1)) end"));
  TEST_ASSERT_EQUAL_STRING("1", check(reg, "c").c_str());
  host.remove("p");
  TEST_ASSERT_EQUAL_STRING("-1", check(reg, "c").c_str());
}

static void test_replacing_a_script_purges_its_shared_keys() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("p", app("def draw() end\ndef setup() shared.set('k', 1) end"));
  host.set("c", app("def draw() end\ndef check() return str(shared.get('p.k', -1)) end"));
  host.set("p", app("def draw() end"));
  TEST_ASSERT_EQUAL_STRING("-1", check(reg, "c").c_str());
}

static void test_install_reserve_unarmed_allows_everything() {
  TEST_ASSERT_TRUE(script::heap::allocFitsReserve(1000, 999, 0));
  TEST_ASSERT_TRUE(script::heap::allocFitsReserve(0, 1 << 20, 0));
}

static void test_install_reserve_refuses_what_would_cross_the_floor() {
  TEST_ASSERT_TRUE(script::heap::allocFitsReserve(40 * 1024, 16 * 1024, 24 * 1024));
  TEST_ASSERT_FALSE(script::heap::allocFitsReserve(40 * 1024, 16 * 1024 + 1, 24 * 1024));
}

static void test_install_reserve_allows_landing_exactly_on_the_floor() {
  TEST_ASSERT_TRUE(script::heap::allocFitsReserve(24 * 1024, 0, 24 * 1024));
  TEST_ASSERT_FALSE(script::heap::allocFitsReserve(24 * 1024 - 1, 0, 24 * 1024));
}

static void test_install_reserve_refuses_an_alloc_larger_than_the_heap() {
  TEST_ASSERT_FALSE(script::heap::allocFitsReserve(1024, 4096, 24 * 1024));
  TEST_ASSERT_FALSE(script::heap::allocFitsReserve(0, 1, 1));
}

namespace {

struct FakeSources {
  std::map<std::string, std::string> src;
  std::map<std::string, std::string> store;

  void wire(script::ScriptServices& svc) {
    svc.readSource = [this](const std::string& n, std::string& out) {
      auto it = src.find(n);
      if (it == src.end()) return false;
      out = it->second;
      return true;
    };
    svc.readStore = [this](const std::string& n, std::string& out) {
      auto it = store.find(n);
      if (it == store.end()) return false;
      out = it->second;
      return true;
    };
  }

  bool set(script::ScriptHost& host, const std::string& name, const std::string& source) {
    src[name] = source;
    return host.set(name, source);
  }

  void remove(script::ScriptHost& host, const std::string& name) {
    src.erase(name);
    host.remove(name);
  }
};

}

static void test_module_is_importable_from_an_app() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  TEST_ASSERT_TRUE(host.set("fmt", "# @module\nvar m = module('fmt')\nm.tag = 'ok'\nreturn m"));
  host.set("clock", "import fmt\n" + app("def draw() end\ndef check() return fmt.tag end"));
  TEST_ASSERT_EQUAL_STRING("ok", check(reg, "clock").c_str());
}

static void test_module_is_not_an_app() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("fmt", "# @module\nreturn module('fmt')");
  TEST_ASSERT_NULL(reg.find("fmt"));
  TEST_ASSERT_TRUE(host.isModule("fmt"));
  TEST_ASSERT_TRUE(host.has("fmt"));
  TEST_ASSERT_EQUAL_UINT(1, host.count());
}

static void test_module_import_name_can_be_overridden() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  TEST_ASSERT_TRUE(
      host.set("weather-lib", "# @module weather\nvar m = module('weather')\nm.tag = 'w'\nreturn m"));
  host.set("clock",
           "import weather\n" + app("def draw() end\ndef check() return weather.tag end"));
  TEST_ASSERT_EQUAL_STRING("w", check(reg, "clock").c_str());
}

static void test_module_with_an_unusable_import_name_is_refused() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  TEST_ASSERT_FALSE(host.set("weather-lib", "# @module\nreturn module('x')"));
  TEST_ASSERT_TRUE(host.lastRefusal().find("not a valid identifier") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT(0, host.count());
}

static void test_module_cannot_shadow_a_builtin() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  TEST_ASSERT_FALSE(host.set("json", "# @module\nreturn module('json')"));
  TEST_ASSERT_TRUE(host.lastRefusal().find("reserved") != std::string::npos);
}

static void test_two_modules_cannot_claim_the_same_import_name() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  TEST_ASSERT_TRUE(host.set("a", "# @module fmt\nreturn module('fmt')"));
  TEST_ASSERT_FALSE(host.set("b", "# @module fmt\nreturn module('fmt')"));
  TEST_ASSERT_TRUE(host.lastRefusal().find("already used by a") != std::string::npos);
}

static void test_module_without_a_return_latches_an_error() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  TEST_ASSERT_TRUE(host.set("fmt", "# @module\nvar m = module('fmt')"));
  TEST_ASSERT_FALSE(host.errorOf("fmt").empty());
  TEST_ASSERT_TRUE(host.isModule("fmt"));
}

static void test_changing_a_module_reloads_its_dependents() {
  AppRegistry reg;
  FakeSources files;
  files.wire(g_svc);
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  files.set(host, "fmt", "# @module\nvar m = module('fmt')\nm.tag = 'one'\nreturn m");
  files.set(host, "clock", "import fmt\n" + app("def draw() end\ndef check() return fmt.tag end"));
  TEST_ASSERT_EQUAL_STRING("one", check(reg, "clock").c_str());

  files.set(host, "fmt", "# @module\nvar m = module('fmt')\nm.tag = 'two'\nreturn m");
  TEST_ASSERT_EQUAL_STRING("two", check(reg, "clock").c_str());
}

static const FontGlyph kMeasureGlyphs[] = {{0, 3, 3, 4, 0, 0}};
static const uint8_t kMeasureBitmap[] = {0xFF, 0x80};
static const GfxFont kMeasureFont = {kMeasureBitmap, kMeasureGlyphs, 'A', 'A', 8};

static std::string measuredByModule(script::ScriptServices& svc, AppRegistry& reg) {
  script::ScriptHost host(reg, svc, nullptr, nullptr);
  host.set("fmt", "# @module\nvar m = module('fmt')\nm.w = text_width('AAAA')\nreturn m");
  host.set("clock", "import fmt\n" + app("def draw() end\ndef check() return str(fmt.w) end"));
  return check(reg, "clock");
}

static void test_a_script_can_read_the_firmware_version() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("V", app("def draw() end\ndef check() return version() end"));
  const std::string v = check(reg, "V");
  TEST_ASSERT_FALSE(v.empty());
  TEST_ASSERT_TRUE(v.find('.') != std::string::npos);
}

static void test_a_module_measures_text_while_it_loads() {
  g_svc.fonts[0] = &kMeasureFont;
  g_svc.fonts[1] = &kMeasureFont;
  AppRegistry reg;
  TEST_ASSERT_EQUAL_STRING("16", measuredByModule(g_svc, reg).c_str());
}

static void test_measuring_without_a_font_reports_unavailable() {
  AppRegistry reg;
  TEST_ASSERT_EQUAL_STRING("-1", measuredByModule(g_svc, reg).c_str());
}

static void test_the_panel_size_is_readable_outside_a_frame() {
  static Canvas panel(32, 8);
  g_svc.panel = &panel;

  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("fmt", "# @module\nvar m = module('fmt')\nm.w = width()\nm.h = height()\nreturn m");
  host.set("clock",
           "import fmt\n" + app("def draw() end\ndef check() return str(fmt.w) + 'x' + str(fmt.h) end"));
  TEST_ASSERT_EQUAL_STRING("32x8", check(reg, "clock").c_str());
}

static void test_sensors_are_readable_while_a_module_loads() {
  static RuntimeState rt;
  rt.hasTemperature = true;
  rt.temperatureC = 21.5f;
  g_svc.runtime = [] { return &rt; };

  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("fmt", "# @module\nvar m = module('fmt')\nm.t = sensor.temperature()\nreturn m");
  host.set("clock",
           "import fmt\n" + app("def draw() end\ndef check() return str(int(fmt.t)) end"));
  TEST_ASSERT_EQUAL_STRING("21", check(reg, "clock").c_str());
}

static void test_removing_a_module_breaks_its_dependents() {
  AppRegistry reg;
  FakeSources files;
  files.wire(g_svc);
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  files.set(host, "fmt", "# @module\nvar m = module('fmt')\nm.tag = 'one'\nreturn m");
  files.set(host, "clock", "import fmt\n" + app("def draw() end\ndef check() return fmt.tag end"));
  TEST_ASSERT_TRUE(host.errorOf("clock").empty());

  files.remove(host, "fmt");
  TEST_ASSERT_FALSE(host.isModule("fmt"));
  TEST_ASSERT_FALSE(host.errorOf("clock").empty());
}

static void test_a_module_can_import_another_module_installed_later() {
  AppRegistry reg;
  FakeSources files;
  files.wire(g_svc);
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  files.set(host, "wrap",
            "# @module\nimport fmt\nvar m = module('wrap')\nm.tag = fmt.tag\nreturn m");
  TEST_ASSERT_FALSE(host.errorOf("wrap").empty());

  files.set(host, "fmt", "# @module\nvar m = module('fmt')\nm.tag = 'deep'\nreturn m");
  TEST_ASSERT_TRUE(host.errorOf("wrap").empty());

  files.set(host, "clock",
            "import wrap\n" + app("def draw() end\ndef check() return wrap.tag end"));
  TEST_ASSERT_EQUAL_STRING("deep", check(reg, "clock").c_str());
}

static void test_turning_an_app_into_a_module_takes_it_out_of_the_registry() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("fmt", app("def draw() end"));
  TEST_ASSERT_NOT_NULL(reg.find("fmt"));

  host.set("fmt", "# @module\nreturn module('fmt')");
  TEST_ASSERT_NULL(reg.find("fmt"));
  TEST_ASSERT_TRUE(host.isModule("fmt"));
  TEST_ASSERT_EQUAL_UINT(1, host.count());
}

static void test_turning_a_module_into_an_app_puts_it_in_the_registry() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("fmt", "# @module\nreturn module('fmt')");
  host.set("fmt", app("def draw() end"));
  TEST_ASSERT_NOT_NULL(reg.find("fmt"));
  TEST_ASSERT_FALSE(host.isModule("fmt"));
  TEST_ASSERT_EQUAL_UINT(1, host.count());
}

static void test_service_rejects_an_unusable_module_name_as_invalid() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  std::vector<std::string> saved;
  script::ScriptService svc(
      host, [&](const std::string& n, const std::string&) { saved.push_back(n); }, nullptr);

  DispatchDetail d;
  TEST_ASSERT_EQUAL(DispatchResult::ValidationError,
                    svc.setScript("weather-lib", "# @module\nreturn module('x')", d));
  TEST_ASSERT_EQUAL_STRING("name", d.field.c_str());
  TEST_ASSERT_EQUAL_UINT(0u, (unsigned)saved.size());

  d.clear();
  TEST_ASSERT_EQUAL(DispatchResult::Ok,
                    svc.setScript("weather-lib", "# @module weather\nreturn module('weather')", d));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)saved.size());
}

// ---- settings a module owns -------------------------------------------------
// The point of the feature: several apps want the same value, so they import
// the module that holds it instead of each declaring its own copy.

static const char* kLocationModule =
    "# @module location\n"
    "# @config city text \"City\" default=\"Berlin\"\n"
    "# @config tint color \"Colour\" default=#FF8800\n"
    "var location = module('location')\n"
    "location.city = store.get('city')\n"
    "location.tint = store.get('tint')\n"
    "return location";

static std::string readerApp() {
  return app("def draw() end\n"
             "def check() import location return str(location.city) end");
}

static void test_a_module_reads_its_own_declared_defaults() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  TEST_ASSERT_TRUE(host.set("location", kLocationModule));
  host.set("Sun", readerApp());
  TEST_ASSERT_EQUAL_STRING("Berlin", check(reg, "Sun").c_str());
}

static void test_a_module_reads_its_stored_value_over_the_default() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("location", kLocationModule, "{\"city\":\"Wien\"}");
  host.set("Sun", readerApp());
  TEST_ASSERT_EQUAL_STRING("Wien", check(reg, "Sun").c_str());
}

static void test_every_app_importing_a_module_sees_one_value() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("location", kLocationModule, "{\"city\":\"Wien\"}");
  host.set("Sun", readerApp());
  host.set("Moon", readerApp());
  TEST_ASSERT_EQUAL_STRING("Wien", check(reg, "Sun").c_str());
  TEST_ASSERT_EQUAL_STRING("Wien", check(reg, "Moon").c_str());
}

static void test_the_module_store_does_not_leak_into_the_app() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("location", kLocationModule, "{\"city\":\"Wien\"}");
  // The app has its own store; the module's keys are not in it.
  host.set("Sun", app("def draw() end\ndef check() return str(store.get('city', 'mine')) end"));
  TEST_ASSERT_EQUAL_STRING("mine", check(reg, "Sun").c_str());
}

static void test_saving_module_settings_restarts_every_importer() {
  FakeSink sink;
  g_svc.storeSink = &sink;
  std::vector<std::string> installed;
  AppRegistry reg;
  script::ScriptHost host(
      reg, g_svc, [&](const std::string& id) { installed.push_back(id); }, nullptr);

  std::map<std::string, std::string> sources;
  std::map<std::string, std::string> stores;
  g_svc.readSource = [&](const std::string& n, std::string& out) {
    auto it = sources.find(n);
    if (it == sources.end()) return false;
    out = it->second;
    return true;
  };
  g_svc.readStore = [&](const std::string& n, std::string& out) {
    auto it = stores.find(n);
    if (it == stores.end()) return false;
    out = it->second;
    return !out.empty();
  };
  script::ScriptService svc(
      host, [&](const std::string& n, const std::string& s) { sources[n] = s; }, nullptr);

  DispatchDetail d;
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("location", kLocationModule, d));
  d.clear();
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("Sun", readerApp(), d));
  d.clear();
  TEST_ASSERT_EQUAL(DispatchResult::Ok, svc.setScript("Idle", app("def draw() end"), d));
  installed.clear();

  d.clear();
  TEST_ASSERT_EQUAL(DispatchResult::Ok,
                    svc.setScriptConfig("location", "{\"city\":\"Graz\"}", d));

  // The importer restarted and reads the new value; the unrelated app did not.
  TEST_ASSERT_EQUAL_STRING("Graz", check(reg, "Sun").c_str());
  TEST_ASSERT_TRUE(std::find(installed.begin(), installed.end(), "Sun") != installed.end());
  TEST_ASSERT_TRUE(std::find(installed.begin(), installed.end(), "Idle") == installed.end());
}

static void test_module_settings_reach_the_store_sink() {
  FakeSink sink;
  g_svc.storeSink = &sink;
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("location",
           "# @module location\n"
           "# @config city text default=\"Berlin\"\n"
           "var location = module('location')\n"
           "store.set('hits', 3)\n"
           "return location");
  bool seen = false;
  for (const std::string& w : sink.writes)
    if (w.find("location:") == 0 && w.find("\"hits\":3") != std::string::npos) seen = true;
  TEST_ASSERT_TRUE(seen);
}

static void test_the_apps_list_reports_a_module_with_settings() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("location", kLocationModule);
  host.set("plain", "# @module plain\nreturn module('plain')");
  const auto list = host.list();
  TEST_ASSERT_TRUE(list.at("location").module);
  TEST_ASSERT_TRUE(list.at("location").config);
  TEST_ASSERT_TRUE(list.at("plain").module);
  TEST_ASSERT_FALSE(list.at("plain").config);
}

// Editing a module's code is the most common thing a user does to it, and it
// must not cost them the settings they chose.
static void test_editing_a_modules_code_keeps_its_settings() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("location", kLocationModule, "{\"city\":\"Wien\"}");
  host.set("Sun", readerApp());
  TEST_ASSERT_EQUAL_STRING("Wien", check(reg, "Sun").c_str());

  const std::string edited = std::string(kLocationModule) + "\n";
  host.set("location", edited, "{\"city\":\"Wien\"}");
  TEST_ASSERT_EQUAL_STRING("Wien", check(reg, "Sun").c_str());
}

// A default the source changes must not overwrite what the user set.
static void test_a_stored_module_value_beats_a_changed_default() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  const char* newDefault =
      "# @module location\n"
      "# @config city text \"City\" default=\"Rom\"\n"
      "var location = module('location')\n"
      "location.city = store.get('city')\n"
      "return location";
  host.set("location", newDefault, "{\"city\":\"Wien\"}");
  host.set("Sun", readerApp());
  TEST_ASSERT_EQUAL_STRING("Wien", check(reg, "Sun").c_str());
}

static void test_removing_a_module_releases_its_store() {
  AppRegistry reg;
  script::ScriptHost host(reg, g_svc, nullptr, nullptr);
  host.set("location", kLocationModule, "{\"city\":\"Wien\"}");
  host.remove("location");
  // Reinstalled with no store, the module falls back to its declared default
  // rather than finding the old value still in the VM.
  host.set("location", kLocationModule);
  host.set("Sun", readerApp());
  TEST_ASSERT_EQUAL_STRING("Berlin", check(reg, "Sun").c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_set_replace_remove_registry_and_hooks);
  RUN_TEST(test_replace_points_registry_at_the_new_app);
  RUN_TEST(test_installing_many_scripts_succeeds);
  RUN_TEST(test_install_refused_over_the_script_heap_budget);
  RUN_TEST(test_a_larger_budget_admits_more_scripts);
  RUN_TEST(test_budget_does_not_relax_the_compile_guards);
  RUN_TEST(test_install_refused_when_system_heap_is_low);
  RUN_TEST(test_install_refused_when_the_heap_is_too_fragmented);
  RUN_TEST(test_low_heap_refusal_is_transient_while_a_fetch_is_in_flight);
  RUN_TEST(test_install_headroom_scales_with_source_size);
  RUN_TEST(test_install_not_blocked_without_a_heap_reading);
  RUN_TEST(test_broken_script_still_installs);
  RUN_TEST(test_tick_runs_loop_at_1hz_and_visibility);
  RUN_TEST(test_loop_runs_for_offscreen_scripts);
  RUN_TEST(test_stagger_spreads_first_loops_across_seconds);
  RUN_TEST(test_reinstall_clears_the_stagger_hold);
  RUN_TEST(test_tls_boot_grace_window_ends);
  RUN_TEST(test_tight_fetch_retries_are_bounded);
  RUN_TEST(test_every_script_is_active_before_a_running_list_arrives);
  RUN_TEST(test_a_script_left_out_of_the_running_list_stops);
  RUN_TEST(test_the_headless_flag_does_not_keep_a_script_running);
  RUN_TEST(test_a_headless_script_needs_no_draw_method);
  RUN_TEST(test_the_headless_flag_follows_a_re_saved_header);
  RUN_TEST(test_rejoining_the_running_list_starts_a_script_again);
  RUN_TEST(test_http_answers_do_not_reach_an_inactive_script);
  RUN_TEST(test_mqtt_messages_do_not_reach_an_inactive_script);
  RUN_TEST(test_button_goes_only_to_the_visible_script);
  RUN_TEST(test_http_result_routed_to_owning_script);
  RUN_TEST(test_http_ids_are_unique_across_scripts);
  RUN_TEST(test_http_failure_and_unknown_id);
  RUN_TEST(test_http_result_for_replaced_script_is_dropped);
  RUN_TEST(test_http_pending_cap_per_script);
  RUN_TEST(test_http_without_transport_soft_fails);
  RUN_TEST(test_mqtt_fans_out_to_every_subscriber);
  RUN_TEST(test_mqtt_publish_reaches_transport);
  RUN_TEST(test_unanswered_http_requests_free_their_slots);
  RUN_TEST(test_answered_http_request_is_not_swept);
  RUN_TEST(test_remove_drops_subscriptions);
  RUN_TEST(test_replace_releases_slots_and_broker_subscriptions);
  RUN_TEST(test_broken_script_does_not_receive_http_results);
  RUN_TEST(test_broken_script_does_not_receive_mqtt_messages);
  RUN_TEST(test_mqtt_wildcard_delivers_the_concrete_topic);
  RUN_TEST(test_mqtt_empty_filter_routes_by_topic);
  RUN_TEST(test_wants_show_asks_the_named_app);
  RUN_TEST(test_wants_show_is_reported_in_the_listing);
  RUN_TEST(test_duration_is_resolved_when_the_app_is_shown);
  RUN_TEST(test_duration_reevaluates_on_each_show);
  RUN_TEST(test_duration_without_the_hook_or_non_positive_is_global);
  RUN_TEST(test_store_write_from_should_show_reaches_the_sink);
  RUN_TEST(test_both_sides_of_a_switch_reach_the_sink);
  RUN_TEST(test_store_writes_routed_to_sink);
  RUN_TEST(test_store_write_from_draw_is_routed_on_next_tick);
  RUN_TEST(test_store_write_is_attributed_to_the_writing_script);
  RUN_TEST(test_store_is_isolated_per_app);
  RUN_TEST(test_declared_defaults_reach_the_store);
  RUN_TEST(test_a_stored_value_wins_over_the_declared_default);
  RUN_TEST(test_a_script_without_config_keeps_its_store_untouched);
  RUN_TEST(test_saving_settings_restarts_the_script_with_the_new_values);
  RUN_TEST(test_the_apps_list_reports_whether_a_script_has_settings);
  RUN_TEST(test_wall_clock_reaches_every_callback);
  RUN_TEST(test_setup_sees_the_last_known_clock);
  RUN_TEST(test_runaway_draw_latches_and_spares_the_rest);
  RUN_TEST(test_runaway_loop_latches_and_spares_the_rest);
  RUN_TEST(test_runaway_setup_latches_at_install);
  RUN_TEST(test_runaway_on_button_latches_and_spares_the_rest);
  RUN_TEST(test_runaway_http_callback_latches_and_spares_the_rest);
  RUN_TEST(test_list_reports_meta_and_errors);
  RUN_TEST(test_destructor_unregisters_apps);
  RUN_TEST(test_service_installs_and_persists);
  RUN_TEST(test_service_stores_a_script_that_does_not_compile);
  RUN_TEST(test_service_refused_for_low_memory_is_capacity_and_stores_nothing);
  RUN_TEST(test_service_resave_preserves_the_store);
  RUN_TEST(test_service_remove_then_install_starts_empty);
  RUN_TEST(test_service_drops_a_setting_the_new_source_no_longer_declares);
  RUN_TEST(test_shared_value_crosses_apps);
  RUN_TEST(test_shared_reads_its_own_namespace_without_a_prefix);
  RUN_TEST(test_shared_get_falls_back_to_the_default);
  RUN_TEST(test_shared_get_without_a_default_is_nil);
  RUN_TEST(test_shared_keeps_the_value_type);
  RUN_TEST(test_shared_age_counts_from_the_write);
  RUN_TEST(test_shared_age_of_a_missing_key_is_nil);
  RUN_TEST(test_shared_set_cannot_reach_another_namespace);
  RUN_TEST(test_shared_set_to_nil_erases_the_key);
  RUN_TEST(test_shared_rejects_non_scalar_values);
  RUN_TEST(test_shared_keys_enumerates_every_provider);
  RUN_TEST(test_shared_keys_can_be_filtered_by_provider);
  RUN_TEST(test_removing_a_script_purges_its_shared_keys);
  RUN_TEST(test_replacing_a_script_purges_its_shared_keys);
  RUN_TEST(test_install_reserve_unarmed_allows_everything);
  RUN_TEST(test_install_reserve_refuses_what_would_cross_the_floor);
  RUN_TEST(test_install_reserve_allows_landing_exactly_on_the_floor);
  RUN_TEST(test_install_reserve_refuses_an_alloc_larger_than_the_heap);
  RUN_TEST(test_module_is_importable_from_an_app);
  RUN_TEST(test_module_is_not_an_app);
  RUN_TEST(test_module_import_name_can_be_overridden);
  RUN_TEST(test_module_with_an_unusable_import_name_is_refused);
  RUN_TEST(test_module_cannot_shadow_a_builtin);
  RUN_TEST(test_two_modules_cannot_claim_the_same_import_name);
  RUN_TEST(test_module_without_a_return_latches_an_error);
  RUN_TEST(test_changing_a_module_reloads_its_dependents);
  RUN_TEST(test_a_script_can_read_the_firmware_version);
  RUN_TEST(test_a_module_measures_text_while_it_loads);
  RUN_TEST(test_measuring_without_a_font_reports_unavailable);
  RUN_TEST(test_the_panel_size_is_readable_outside_a_frame);
  RUN_TEST(test_sensors_are_readable_while_a_module_loads);
  RUN_TEST(test_removing_a_module_breaks_its_dependents);
  RUN_TEST(test_a_module_can_import_another_module_installed_later);
  RUN_TEST(test_turning_an_app_into_a_module_takes_it_out_of_the_registry);
  RUN_TEST(test_turning_a_module_into_an_app_puts_it_in_the_registry);
  RUN_TEST(test_service_rejects_an_unusable_module_name_as_invalid);
  RUN_TEST(test_a_module_reads_its_own_declared_defaults);
  RUN_TEST(test_a_module_reads_its_stored_value_over_the_default);
  RUN_TEST(test_every_app_importing_a_module_sees_one_value);
  RUN_TEST(test_the_module_store_does_not_leak_into_the_app);
  RUN_TEST(test_saving_module_settings_restarts_every_importer);
  RUN_TEST(test_module_settings_reach_the_store_sink);
  RUN_TEST(test_the_apps_list_reports_a_module_with_settings);
  RUN_TEST(test_editing_a_modules_code_keeps_its_settings);
  RUN_TEST(test_a_stored_module_value_beats_a_changed_default);
  RUN_TEST(test_removing_a_module_releases_its_store);
  return UNITY_END();
}
