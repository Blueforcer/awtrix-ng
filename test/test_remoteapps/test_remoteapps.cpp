#include <unity.h>

#include <string>
#include <vector>

#include "core/apps/AppRegistry.h"
#include "core/remote/RemoteApps.h"
#include "core/remote/RemoteFrames.h"
#include "platform/linux/host/HostScriptHeap.h"

using namespace awtrix;
using namespace awtrix::remote;

namespace {

struct Sent {
  std::string method;
  std::string path;
  std::string body;
};

struct Fixture {
  AppRegistry registry;
  RemoteFrames frames;
  std::vector<std::string> added;
  std::vector<std::string> removed;
  std::vector<bool> holds;
  std::vector<Sent> sent;
  bool accept = true;
  RemoteApps apps{registry, frames, [this](const std::string& n) { added.push_back(n); },
                  [this](const std::string& n) { removed.push_back(n); },
                  [this](bool hold) { holds.push_back(hold); }};
  Fixture() {
    frames.begin(32, 8);
    apps.setForward([this](const char* method, const std::string& path, std::string body) {
      sent.push_back({method, path, std::move(body)});
      return accept;
    });
  }
};

wire::Apps list(std::initializer_list<wire::AppEntry> entries, uint16_t revision = 1) {
  wire::Apps a;
  a.revision = revision;
  for (const wire::AppEntry& e : entries) a.entries[a.count++] = e;
  return a;
}

wire::AppEntry entry(const char* name, uint8_t flags = wire::app::kWantsShow, uint32_t durationMs = 0) {
  wire::AppEntry e;
  e.name = name;
  e.flags = flags;
  e.durationMs = durationMs;
  return e;
}

int rc(DispatchResult r) { return static_cast<int>(r); }

}

void setUp() {}
void tearDown() { script::heap::testing::resetGrowthBudget(); }

static void test_listed_apps_join_the_registry_and_the_engine() {
  Fixture f;
  f.apps.apply(list({entry("Weather"), entry("Stocks")}));
  TEST_ASSERT_NOT_NULL(f.registry.find("Weather"));
  TEST_ASSERT_NOT_NULL(f.registry.find("Stocks"));
  TEST_ASSERT_EQUAL(2, f.added.size());
  TEST_ASSERT_TRUE(f.apps.owns("Weather"));
  TEST_ASSERT_FALSE(f.apps.owns("Time"));
  TEST_ASSERT_EQUAL(1, f.apps.revision());
}

static void test_an_app_missing_from_the_next_list_leaves() {
  Fixture f;
  f.apps.apply(list({entry("Weather"), entry("Stocks")}));
  f.apps.apply(list({entry("Stocks")}, 2));
  TEST_ASSERT_NULL(f.registry.find("Weather"));
  TEST_ASSERT_NOT_NULL(f.registry.find("Stocks"));
  TEST_ASSERT_EQUAL(1, f.removed.size());
  TEST_ASSERT_EQUAL_STRING("Weather", f.removed[0].c_str());
  TEST_ASSERT_EQUAL_MESSAGE(2, f.added.size(), "an app already known is not announced again");
}

static void test_a_list_already_applied_is_not_read_again() {
  Fixture f;
  f.apps.apply(list({entry("Weather")}, 7));
  f.apps.apply(list({}, 7));
  TEST_ASSERT_TRUE(f.apps.owns("Weather"));
  f.apps.apply(list({}, 8));
  TEST_ASSERT_FALSE(f.apps.owns("Weather"));
}

static void test_without_a_link_no_app_wants_to_show() {
  Fixture f;
  f.apps.apply(list({entry("Weather")}));
  TEST_ASSERT_FALSE_MESSAGE(f.apps.scriptWantsShow("Weather"), "the link starts down");
  f.apps.setLinkUp(true);
  TEST_ASSERT_TRUE(f.apps.scriptWantsShow("Weather"));
  f.apps.setLinkUp(false);
  TEST_ASSERT_FALSE(f.apps.scriptWantsShow("Weather"));
}

static void test_flags_and_duration_come_from_the_host() {
  Fixture f;
  f.apps.setLinkUp(true);
  f.apps.apply(list({entry("Game", wire::app::kWantsShow | wire::app::kTakesButtons | wire::app::kScrollHolds, 30000),
                     entry("Quiet", 0)}));
  TEST_ASSERT_TRUE(f.apps.takesButtons("Game"));
  TEST_ASSERT_TRUE(f.apps.scriptScrollHolds("Game"));
  TEST_ASSERT_EQUAL(30000, f.apps.scriptDurationMs("Game"));
  TEST_ASSERT_FALSE(f.apps.takesButtons("Quiet"));
  TEST_ASSERT_FALSE(f.apps.scriptWantsShow("Quiet"));
  TEST_ASSERT_FALSE(f.apps.scriptWantsShow("Unknown"));
  f.apps.apply(list({entry("Game", wire::app::kWantsShow, 30000), entry("Quiet")}, 2));
  TEST_ASSERT_FALSE_MESSAGE(f.apps.takesButtons("Game"), "a new list replaces the flags");
  TEST_ASSERT_TRUE(f.apps.scriptWantsShow("Quiet"));
}

static void test_buttons_and_scroll_holds_end_with_the_link() {
  Fixture f;
  f.apps.apply(list({entry("Game", wire::app::kTakesButtons | wire::app::kScrollHolds)}));
  TEST_ASSERT_FALSE(f.apps.takesButtons("Game"));
  TEST_ASSERT_FALSE(f.apps.scriptScrollHolds("Game"));
  f.apps.setLinkUp(true);
  TEST_ASSERT_TRUE(f.apps.takesButtons("Game"));
  TEST_ASSERT_TRUE(f.apps.scriptScrollHolds("Game"));
}

static void test_the_host_hold_ends_with_the_link() {
  Fixture f;
  f.apps.hold(true);
  TEST_ASSERT_EQUAL(1, f.holds.size());
  TEST_ASSERT_FALSE_MESSAGE(f.holds[0], "no hold without a link");
  f.apps.setLinkUp(true);
  f.apps.hold(true);
  f.apps.hold(true);
  TEST_ASSERT_EQUAL_MESSAGE(3, f.holds.size(), "each hold applies again, the clock may have let go meanwhile");
  TEST_ASSERT_TRUE(f.holds[2]);
  f.apps.setLinkUp(false);
  TEST_ASSERT_EQUAL(4, f.holds.size());
  TEST_ASSERT_FALSE(f.holds[3]);
  f.apps.setLinkUp(false);
  TEST_ASSERT_EQUAL_MESSAGE(4, f.holds.size(), "a hold not set is not released");
}

static void test_headless_and_on_demand_apps_are_told_apart() {
  Fixture f;
  f.apps.apply(list({entry("Sensor", wire::app::kHeadless), entry("Doom", wire::app::kOnDemand), entry("Clock")}));
  TEST_ASSERT_TRUE(f.apps.scriptIsHeadless("Sensor"));
  TEST_ASSERT_FALSE(f.apps.scriptIsHeadless("Clock"));
  TEST_ASSERT_TRUE(f.apps.scriptIsOnDemand("Doom"));
  TEST_ASSERT_FALSE(f.apps.scriptIsOnDemand("Clock"));
  f.apps.apply(list({entry("Sensor", wire::app::kHeadless), entry("Doom", wire::app::kOnDemand),
                     entry("Clock", wire::app::kOnDemand)}, 2));
  TEST_ASSERT_EQUAL_MESSAGE(4, f.added.size(), "an app that became on-demand is filed again");
  TEST_ASSERT_EQUAL_STRING("Clock", f.added.back().c_str());
  f.apps.apply(list({entry("Sensor", wire::app::kHeadless | wire::app::kTakesButtons),
                     entry("Doom", wire::app::kOnDemand), entry("Clock", wire::app::kOnDemand)}, 3));
  TEST_ASSERT_EQUAL_MESSAGE(4, f.added.size(), "other flags do not file it again");
}

static void test_an_on_demand_app_starts_only_while_linked() {
  Fixture f;
  f.apps.apply(list({entry("Doom", wire::app::kOnDemand), entry("Clock")}));
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(rc(DispatchResult::OnHost), rc(f.apps.launchScript("Doom", detail)));
  TEST_ASSERT_EQUAL_STRING("script host not reachable", detail.message.c_str());
  f.apps.setLinkUp(true);
  TEST_ASSERT_EQUAL(rc(DispatchResult::Ok), rc(f.apps.launchScript("Doom", detail)));
  TEST_ASSERT_EQUAL(rc(DispatchResult::NotFound), rc(f.apps.launchScript("Clock", detail)));
  TEST_ASSERT_EQUAL(rc(DispatchResult::NotFound), rc(f.apps.launchScript("Nope", detail)));
}

static void test_switched_off_apps_make_the_mask_by_list_position() {
  Fixture f;
  f.apps.apply(list({entry("A"), entry("Doom", wire::app::kOnDemand), entry("B"), entry("C")}));
  f.apps.setRunningScripts({"B"});
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0b1001, f.apps.offMask(), "on-demand apps are never off");
  f.apps.apply(list({entry("C"), entry("A"), entry("B")}, 2));
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0b011, f.apps.offMask(), "the mask follows the new order");
  f.apps.setRunningScripts({"A", "B", "C"});
  TEST_ASSERT_EQUAL_HEX32(0, f.apps.offMask());
}

static void test_script_changes_go_on_to_the_host_while_linked() {
  Fixture f;
  f.apps.setLinkUp(true);
  f.apps.apply(list({entry("Weather")}));
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(rc(DispatchResult::Ok), rc(f.apps.setScript("Weather", "return 1", detail)));
  TEST_ASSERT_EQUAL(rc(DispatchResult::Ok), rc(f.apps.updateScript("Weather", "{\"source\":\"x\"}", detail)));
  TEST_ASSERT_EQUAL(rc(DispatchResult::Ok), rc(f.apps.setScriptConfig("Weather", "{\"a\":1}", detail)));
  TEST_ASSERT_EQUAL(rc(DispatchResult::Ok), rc(f.apps.setScriptData("Weather", "{\"b\":2}", detail)));
  TEST_ASSERT_EQUAL(rc(DispatchResult::Ok), rc(f.apps.removeScript("Weather", detail)));
  TEST_ASSERT_EQUAL(5, f.sent.size());
  TEST_ASSERT_EQUAL_STRING("PUT", f.sent[0].method.c_str());
  TEST_ASSERT_EQUAL_STRING("/api/v1/apps/script/Weather", f.sent[0].path.c_str());
  TEST_ASSERT_EQUAL_STRING("return 1", f.sent[0].body.c_str());
  TEST_ASSERT_EQUAL_STRING("/api/v1/apps/script-update/Weather", f.sent[1].path.c_str());
  TEST_ASSERT_EQUAL_STRING("PATCH", f.sent[2].method.c_str());
  TEST_ASSERT_EQUAL_STRING("/api/v1/apps/Weather/config", f.sent[2].path.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", f.sent[2].body.c_str());
  TEST_ASSERT_EQUAL_STRING("/api/v1/apps/Weather/data", f.sent[3].path.c_str());
  TEST_ASSERT_EQUAL_STRING("DELETE", f.sent[4].method.c_str());
  TEST_ASSERT_EQUAL_STRING("/api/v1/apps/Weather", f.sent[4].path.c_str());
  TEST_ASSERT_NOT_NULL_MESSAGE(f.registry.find("Weather"), "the app leaves once the host stops listing it");
}

static void test_script_changes_are_refused_while_the_host_is_away() {
  Fixture f;
  f.apps.apply(list({entry("Weather")}));
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(rc(DispatchResult::OnHost), rc(f.apps.setScript("Weather", "return 1", detail)));
  TEST_ASSERT_EQUAL_STRING("script host not reachable", detail.message.c_str());
  TEST_ASSERT_EQUAL(rc(DispatchResult::OnHost), rc(f.apps.removeScript("Weather", detail)));
  TEST_ASSERT_EQUAL(rc(DispatchResult::OnHost), rc(f.apps.setScriptConfig("Weather", "{}", detail)));
  TEST_ASSERT_EQUAL(rc(DispatchResult::OnHost), rc(f.apps.setScriptData("Weather", "{}", detail)));
  TEST_ASSERT_EQUAL(rc(DispatchResult::OnHost), rc(f.apps.updateScript("Weather", "{}", detail)));
  TEST_ASSERT_EQUAL(0, f.sent.size());
  f.apps.setLinkUp(true);
  f.accept = false;
  detail = DispatchDetail{};
  TEST_ASSERT_EQUAL_MESSAGE(rc(DispatchResult::OnHost), rc(f.apps.setScript("Weather", "return 1", detail)),
                            "a request the worker cannot take is refused too");
  TEST_ASSERT_EQUAL_STRING("script host not reachable", detail.message.c_str());
}

static void test_removing_an_app_the_host_does_not_list_needs_no_host() {
  Fixture f;
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(rc(DispatchResult::Ok), rc(f.apps.removeScript("pushed", detail)));
  TEST_ASSERT_EQUAL(0, f.sent.size());
}

static void test_a_source_bigger_than_the_heap_allows_is_refused_before_the_copy() {
  Fixture f;
  f.apps.setLinkUp(true);
  script::heap::testing::setGrowthBudget(100);
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(rc(DispatchResult::Capacity), rc(f.apps.setScript("Big", std::string(101, 'x'), detail)));
  TEST_ASSERT_EQUAL_STRING("not enough memory", detail.message.c_str());
  TEST_ASSERT_EQUAL(0, f.sent.size());
  TEST_ASSERT_EQUAL(rc(DispatchResult::Ok), rc(f.apps.setScript("Small", std::string(100, 'x'), detail)));
}

static void test_an_app_draws_its_last_frame_or_black() {
  Fixture f;
  f.apps.apply(list({entry("Weather")}));
  IApp* app = f.registry.find("Weather");
  TEST_ASSERT_NOT_NULL(app);
  Canvas out(32, 8);
  out.clear(0xFFFFFF);
  RenderCtx ctx;
  app->render(out, ctx);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x000000, out.getPixel(0, 0), "black before the first frame");
  Canvas src(32, 8);
  src.clear(0x00FF00);
  uint8_t buf[wire::kMaxDatagram];
  wire::Packet p;
  TEST_ASSERT_TRUE(wire::decode(buf, wire::encodeFrame(src, "Weather", 1, 0, 8, buf, sizeof(buf)), p));
  f.frames.want("Weather", "");
  f.frames.receive(p.frame, 0);
  app->render(out, ctx);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, out.getPixel(0, 0));
}

static void test_leaving_the_clock_cleans_the_registry() {
  AppRegistry registry;
  RemoteFrames frames;
  {
    RemoteApps apps(registry, frames, nullptr, nullptr, nullptr);
    apps.apply(list({entry("Weather")}));
    TEST_ASSERT_EQUAL(1, registry.size());
  }
  TEST_ASSERT_EQUAL(0, registry.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_listed_apps_join_the_registry_and_the_engine);
  RUN_TEST(test_an_app_missing_from_the_next_list_leaves);
  RUN_TEST(test_a_list_already_applied_is_not_read_again);
  RUN_TEST(test_without_a_link_no_app_wants_to_show);
  RUN_TEST(test_flags_and_duration_come_from_the_host);
  RUN_TEST(test_buttons_and_scroll_holds_end_with_the_link);
  RUN_TEST(test_the_host_hold_ends_with_the_link);
  RUN_TEST(test_headless_and_on_demand_apps_are_told_apart);
  RUN_TEST(test_an_on_demand_app_starts_only_while_linked);
  RUN_TEST(test_switched_off_apps_make_the_mask_by_list_position);
  RUN_TEST(test_script_changes_go_on_to_the_host_while_linked);
  RUN_TEST(test_script_changes_are_refused_while_the_host_is_away);
  RUN_TEST(test_removing_an_app_the_host_does_not_list_needs_no_host);
  RUN_TEST(test_a_source_bigger_than_the_heap_allows_is_refused_before_the_copy);
  RUN_TEST(test_an_app_draws_its_last_frame_or_black);
  RUN_TEST(test_leaving_the_clock_cleans_the_registry);
  return UNITY_END();
}
