#include <unity.h>

#include <algorithm>
#include <map>
#include <vector>

#include "core/CoreEngine.h"
#include "core/api/StateJson.h"
#include "core/effects/EffectRegistry.h"
#include "core/script/ScriptSourceService.h"

using namespace awtrix;

namespace {
struct FDisplay : IDisplayService {
  void sendScreen() override {}
};
struct FSystem : ISystemService {
  void reboot() override {}
  void sleep(uint64_t) override {}
  void factoryReset() override {}
  void resetSettings() override {}
};

struct FEffect : IEffect {
  std::string id_;
  explicit FEffect(std::string n) : id_(std::move(n)) {}
  const std::string& id() const override { return id_; }
  void render(Canvas&, int64_t) override {}
};

int rc(DispatchResult r) { return static_cast<int>(r); }

Command cmd(CommandType t, const std::string& name = "", const std::string& payload = "", int arg = 0,
           bool clear = false) {
  Command c(t);
  c.name = name;
  c.payload = payload;
  c.arg = arg;
  c.clear = clear;
  return c;
}
}

void setUp() {}
void tearDown() {}

static void test_default_app_list() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_UINT(5u, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_STRING("Time", e.currentAppId().c_str());
}

static void test_custom_app_add_and_delete() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
                        rc(e.execute(cmd(CommandType::SetPushedApp, "weather", "{\"text\":\"x\"}"))));
  TEST_ASSERT_NOT_NULL(e.pushedApp("weather"));
  TEST_ASSERT_EQUAL_UINT(6u, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
                        rc(e.execute(cmd(CommandType::SetPushedApp, "weather", "", 0, true))));
  TEST_ASSERT_NULL(e.pushedApp("weather"));
  TEST_ASSERT_EQUAL_UINT(5u, (unsigned)e.appHost().count());
}

static void test_pushed_apps_follow_the_order_they_arrived_in() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetPushedApp, "zulu", "{\"text\":\"x\"}"));
  e.execute(cmd(CommandType::SetPushedApp, "alpha", "{\"text\":\"x\"}"));
  e.execute(cmd(CommandType::SetPushedApp, "mike", "{\"text\":\"x\"}"));
  const auto& ids = e.appHost().ids();
  TEST_ASSERT_EQUAL_UINT(8u, (unsigned)ids.size());
  TEST_ASSERT_EQUAL_STRING("zulu", ids[5].c_str());
  TEST_ASSERT_EQUAL_STRING("alpha", ids[6].c_str());
  TEST_ASSERT_EQUAL_STRING("mike", ids[7].c_str());
}

static void test_updating_a_pushed_app_leaves_its_place_alone() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetPushedApp, "zulu", "{\"text\":\"x\"}"));
  e.execute(cmd(CommandType::SetPushedApp, "alpha", "{\"text\":\"x\"}"));
  e.execute(cmd(CommandType::SetPushedApp, "zulu", "{\"text\":\"y\"}"));
  const auto& ids = e.appHost().ids();
  TEST_ASSERT_EQUAL_STRING("zulu", ids[5].c_str());
  TEST_ASSERT_EQUAL_STRING("alpha", ids[6].c_str());
}

static void test_a_returning_pushed_app_lands_at_the_end() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetPushedApp, "zulu", "{\"text\":\"x\"}"));
  e.execute(cmd(CommandType::SetPushedApp, "alpha", "{\"text\":\"x\"}"));
  e.execute(cmd(CommandType::SetPushedApp, "zulu", "", 0, true));
  e.execute(cmd(CommandType::SetPushedApp, "zulu", "{\"text\":\"x\"}"));
  const auto& ids = e.appHost().ids();
  TEST_ASSERT_EQUAL_STRING("alpha", ids[5].c_str());
  TEST_ASSERT_EQUAL_STRING("zulu", ids[6].c_str());
}

static void test_an_array_push_keeps_its_element_order() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetPushedApp, "arr",
                "[{\"text\":\"a\"},{\"text\":\"b\"},{\"text\":\"c\"}]"));
  const auto& ids = e.appHost().ids();
  TEST_ASSERT_EQUAL_STRING("arr0", ids[5].c_str());
  TEST_ASSERT_EQUAL_STRING("arr1", ids[6].c_str());
  TEST_ASSERT_EQUAL_STRING("arr2", ids[7].c_str());
}

static void test_an_arranged_app_keeps_its_slot_when_others_arrive() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetPushedApp, "zulu", "{\"text\":\"x\"}"));
  e.execute(cmd(CommandType::SetAppOrder, "",
                "{\"order\":[\"zulu\",\"Time\"],\"disabled\":[\"Date\",\"Temperature\",\"Humidity\",\"Battery\"]}"));
  e.execute(cmd(CommandType::SetPushedApp, "alpha", "{\"text\":\"x\"}"));
  const auto& ids = e.appHost().ids();
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)ids.size());
  TEST_ASSERT_EQUAL_STRING("zulu", ids[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Time", ids[1].c_str());
  TEST_ASSERT_EQUAL_STRING("alpha", ids[2].c_str());
}

static void test_array_custom_apps_indexed() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SetPushedApp, "multi", "[{\"text\":\"a\"},{\"text\":\"b\"}]"))));
  TEST_ASSERT_NOT_NULL(e.pushedApp("multi0"));
  TEST_ASSERT_NOT_NULL(e.pushedApp("multi1"));
  e.execute(cmd(CommandType::SetPushedApp, "multi", "", 0, true));
  TEST_ASSERT_NULL(e.pushedApp("multi0"));
  TEST_ASSERT_NULL(e.pushedApp("multi1"));
}

static void test_custom_app_capacity_is_507() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  for (int i = 0; i < 50; ++i) {
    TEST_ASSERT_EQUAL_INT(
        rc(DispatchResult::Ok),
        rc(e.execute(cmd(CommandType::SetPushedApp, "app" + std::to_string(i), "{\"text\":\"x\"}"))));
  }
  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::Capacity),
      rc(e.execute(cmd(CommandType::SetPushedApp, "app50", "{\"text\":\"x\"}"))));
  TEST_ASSERT_NULL(e.pushedApp("app50"));
  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SetPushedApp, "app0", "{\"text\":\"y\"}"))));
}

static void test_notification_capacity_is_507() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  for (int i = 0; i < 32; ++i) {
    TEST_ASSERT_EQUAL_INT(
        rc(DispatchResult::Ok),
        rc(e.execute(cmd(CommandType::Notify, "", "{\"text\":\"n\",\"stack\":true}"))));
  }
  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::Capacity),
      rc(e.execute(cmd(CommandType::Notify, "", "{\"text\":\"n\",\"stack\":true}"))));
}

static void test_notify_and_dismiss() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
                        rc(e.execute(cmd(CommandType::Notify, "", "{\"text\":\"hi\"}"))));
  TEST_ASSERT_TRUE(e.hasNotification());
  TEST_ASSERT_EQUAL_STRING("hi", e.notifications().current().text.c_str());
  e.execute(cmd(CommandType::DismissNotify));
  TEST_ASSERT_FALSE(e.hasNotification());
}

static void test_switch_app() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
                        rc(e.execute(cmd(CommandType::SwitchApp, "", "Humidity"))));
  TEST_ASSERT_TRUE(e.appHost().inTransition());
  e.tick(1000);
  TEST_ASSERT_EQUAL_STRING("Humidity", e.currentAppId().c_str());
  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SwitchApp, "", "{\"name\":\"Time\",\"fast\":true}"))));
  TEST_ASSERT_EQUAL_STRING("Time", e.currentAppId().c_str());
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::NotFound),
                        rc(e.execute(cmd(CommandType::SwitchApp, "", "Nope"))));
}

static void test_the_disabled_list_decides_what_stays_out() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Humidity\",\"Time\"],\"disabled\":[\"Date\",\"Temperature\",\"Battery\"]}"))));
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_STRING("Humidity", e.appHost().ids()[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Time", e.appHost().ids()[1].c_str());
}

static void test_an_app_that_turns_up_later_joins_the_loop() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\"],\"disabled\":[\"Date\",\"Temperature\",\"Humidity\",\"Battery\"]}"));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)e.appHost().count());
  e.execute(cmd(CommandType::SetPushedApp, "weather", "{\"text\":\"x\"}"));
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_STRING("weather", e.appHost().ids()[1].c_str());
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SetAppOrder, "",
                       "{\"order\":[\"Temperature\",\"Time\",\"weather\"],\"disabled\":[\"Date\",\"Humidity\",\"Battery\"]}"))));
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_STRING("Temperature", e.appHost().ids()[0].c_str());
}

static void test_a_pushed_app_replaces_the_builtin_of_the_same_name() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  const unsigned before = (unsigned)e.appHost().count();
  TEST_ASSERT_EQUAL_STRING("Time", e.appHost().ids()[0].c_str());
  e.execute(cmd(CommandType::SetPushedApp, "Time", "{\"text\":\"x\"}"));
  TEST_ASSERT_EQUAL_UINT(before, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_STRING("Time", e.appHost().ids()[before - 1].c_str());
  e.deletePushedApp("Time");
  TEST_ASSERT_EQUAL_UINT(before, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_STRING("Time", e.appHost().ids()[0].c_str());
}

static void test_order_allows_duplicate_apps() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetSettings, "", "{\"transitionDurationMs\":400}"));
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SetAppOrder, "",
                       "{\"order\":[\"Time\",\"Temperature\",\"Time\",\"Humidity\"],\"disabled\":[\"Date\",\"Battery\"]}"))));
  TEST_ASSERT_EQUAL_UINT(4u, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_STRING("Time", e.appHost().ids()[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Time", e.appHost().ids()[2].c_str());
  e.tick(0);
  e.tick(7000); e.tick(7400);
  TEST_ASSERT_EQUAL_STRING("Temperature", e.currentAppId().c_str());
  e.tick(14400); e.tick(14800);
  TEST_ASSERT_EQUAL_STRING("Time", e.currentAppId().c_str());
  TEST_ASSERT_EQUAL_INT(2, e.appHost().currentIndex());
}

static void test_order_reserves_spot_for_unknown_apps() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SetAppOrder, "",
                       "{\"order\":[\"Time\",\"weather\",\"Temperature\"],\"disabled\":[\"Date\",\"Humidity\",\"Battery\"]}"))));
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_STRING("Time", e.appHost().ids()[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Temperature", e.appHost().ids()[1].c_str());
  e.execute(cmd(CommandType::SetPushedApp, "weather", "{\"text\":\"21Â°\"}"));
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_STRING("weather", e.appHost().ids()[1].c_str());
}

static void test_apps_inventory_includes_switched_off_apps() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\"],\"disabled\":[\"Date\",\"ghost\"]}"));
  const auto all = e.allApps();
  TEST_ASSERT_EQUAL_UINT(6u, (unsigned)all.size());
  TEST_ASSERT_TRUE(std::find(all.begin(), all.end(), "ghost") != all.end());
  TEST_ASSERT_FALSE(e.isEnabled("ghost"));
  TEST_ASSERT_FALSE(e.isPresent("ghost"));
}

static void test_per_app_duration_overrides_global() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetSettings, "", "{\"transitionDurationMs\":400}"));
  e.execute(cmd(CommandType::SetPushedApp, "fast", "{\"text\":\"x\",\"durationMs\":2000}"));
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"fast\",\"Time\"],\"disabled\":[\"Date\",\"Temperature\",\"Humidity\",\"Battery\"]}"));
  e.execute(cmd(CommandType::SwitchApp, "", "{\"name\":\"fast\",\"fast\":true}"));
  e.tick(0);
  TEST_ASSERT_EQUAL_STRING("fast", e.currentAppId().c_str());
  e.tick(1500);
  TEST_ASSERT_EQUAL_STRING("fast", e.currentAppId().c_str());
  TEST_ASSERT_FALSE(e.appHost().inTransition());
  e.tick(2000);
  TEST_ASSERT_TRUE(e.appHost().inTransition());
  e.tick(2500);
  TEST_ASSERT_EQUAL_STRING("Time", e.currentAppId().c_str());
}

static void test_frequent_pushes_do_not_freeze_the_rotation() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetSettings, "", "{\"transitionDurationMs\":1000}"));
  e.execute(cmd(CommandType::SetPushedApp, "weather", "{\"text\":\"21\"}"));
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\",\"weather\"],\"disabled\":[\"Date\",\"Temperature\",\"Humidity\",\"Battery\"]}"));
  e.execute(cmd(CommandType::SwitchApp, "", "{\"name\":\"Time\",\"fast\":true}"));
  for (int64_t t = 0; t <= 9000; t += 500) {
    e.tick(t);
    e.execute(cmd(CommandType::SetPushedApp, "weather", "{\"text\":\"21\"}"));
  }
  TEST_ASSERT_EQUAL_STRING("weather", e.currentAppId().c_str());
}

static void test_unknown_effect_or_overlay_name_is_rejected() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  EffectRegistry effects, overlays;
  FEffect plasma("Plasma"), rain("rain");
  effects.add(&plasma);
  overlays.add(&rain);
  e.setEffectRegistry(&effects);
  e.setOverlayRegistry(&overlays);

  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::ValidationError),
      rc(e.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"x\",\"effect\":\"Nope\"}"))));
  TEST_ASSERT_NULL(e.pushedApp("a"));
  TEST_ASSERT_EQUAL_STRING("effect", e.lastDetail().field.c_str());

  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::ValidationError),
      rc(e.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"x\",\"overlay\":\"nope\"}"))));
  TEST_ASSERT_NULL(e.pushedApp("a"));

  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"x\",\"effect\":\"plasma\"}"))));
  TEST_ASSERT_NOT_NULL(e.pushedApp("a"));

  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::ValidationError),
      rc(e.execute(cmd(CommandType::Notify, "", "{\"text\":\"n\",\"effect\":\"Nope\"}"))));
  TEST_ASSERT_FALSE(e.hasNotification());
}

static void test_bad_scroll_in_a_payload_is_rejected() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);

  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::ValidationError),
      rc(e.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"x\",\"scroll\":{\"speed\":-5}}"))));
  TEST_ASSERT_NULL(e.pushedApp("a"));
  TEST_ASSERT_EQUAL_STRING("scroll.speed", e.lastDetail().field.c_str());

  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::ValidationError),
      rc(e.execute(cmd(CommandType::SetPushedApp, "a", "{\"scroll\":{\"speeed\":5}}"))));
  TEST_ASSERT_EQUAL_STRING("scroll.speeed", e.lastDetail().field.c_str());

  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::ValidationError),
      rc(e.execute(cmd(CommandType::Notify, "", "{\"text\":\"n\",\"scroll\":\"pingpong\"}"))));
  TEST_ASSERT_FALSE(e.hasNotification());

  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SetPushedApp, "a", "{\"text\":\"x\",\"scroll\":\"bounce\"}"))));
  TEST_ASSERT_NOT_NULL(e.pushedApp("a"));
}

static void test_notification_array_is_rejected_not_truncated() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::ValidationError),
      rc(e.execute(cmd(CommandType::Notify, "", "[{\"text\":\"a\"},{\"text\":\"b\"}]"))));
  TEST_ASSERT_FALSE(e.hasNotification());

  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
                        rc(e.execute(cmd(CommandType::Notify, "", "[{\"text\":\"a\"}]"))));
  TEST_ASSERT_TRUE(e.hasNotification());
}

static void test_humidity_app_gated_on_sensor_capability() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  auto has = [&](const char* n) {
    const auto a = e.allApps();
    return std::find(a.begin(), a.end(), std::string(n)) != a.end();
  };
  TEST_ASSERT_TRUE(has("Humidity"));
  e.setHumidityAvailable(false);
  TEST_ASSERT_FALSE(has("Humidity"));
  TEST_ASSERT_TRUE(has("Temperature"));
}

static void test_temperature_app_gated_on_sensor_capability() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  auto has = [&](const char* n) {
    const auto a = e.allApps();
    return std::find(a.begin(), a.end(), std::string(n)) != a.end();
  };
  TEST_ASSERT_TRUE(has("Temperature"));
  e.setTemperatureAvailable(false);
  TEST_ASSERT_FALSE(has("Temperature"));
  TEST_ASSERT_TRUE(has("Time"));
  e.setTemperatureAvailable(true);
  TEST_ASSERT_TRUE(has("Temperature"));
}

static void test_settings_change_leaves_loop_alone() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\",\"Date\"],\"disabled\":[\"Temperature\",\"Humidity\",\"Battery\"]}"))));
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
                        rc(e.execute(cmd(CommandType::SetSettings, "", "{\"brightness\":200}"))));
  TEST_ASSERT_EQUAL_INT(200, e.state().settings().brightness);
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_STRING("Date", e.appHost().ids()[1].c_str());
}

static void test_bus_submit_then_tick_drains() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_TRUE(e.submit(cmd(CommandType::Notify, "", "{\"text\":\"q\"}")));
  TEST_ASSERT_FALSE(e.hasNotification());
  e.tick(0);
  TEST_ASSERT_TRUE(e.hasNotification());
}

static void test_auto_rotation() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetSettings, "", "{\"transitionDurationMs\":400}"));
  TEST_ASSERT_EQUAL_STRING("Time", e.currentAppId().c_str());
  e.tick(0);
  e.tick(7000);
  e.tick(7400);
  TEST_ASSERT_EQUAL_STRING("Date", e.currentAppId().c_str());
}

static void test_script_app_declining_is_skipped_by_the_rotation() {
  struct FScripts : IScriptService {
    std::vector<std::string> asked;
    std::string quiet;
    DispatchResult setScript(const std::string&, const std::string&, DispatchDetail&) override {
      return DispatchResult::Ok;
    }
    void removeScript(const std::string&) override {}
    bool scriptWantsShow(const std::string& name) override {
      asked.push_back(name);
      return name != quiet;
    }
  } scripts;
  scripts.quiet = "Silent";

  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.setScriptService(&scripts);
  e.execute(cmd(CommandType::SetSettings, "", "{\"transitionDurationMs\":400}"));
  e.syncScriptApp("Silent");
  e.syncScriptApp("Loud");
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\",\"Silent\",\"Loud\"],\"disabled\":[\"Date\",\"Temperature\",\"Humidity\",\"Battery\"]}"));
  TEST_ASSERT_EQUAL_STRING("Time", e.currentAppId().c_str());

  e.tick(0);
  e.tick(7000);
  e.tick(7400);
  TEST_ASSERT_EQUAL_STRING("Loud", e.currentAppId().c_str());
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)scripts.asked.size());
  TEST_ASSERT_EQUAL_STRING("Silent", scripts.asked[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Loud", scripts.asked[1].c_str());

  TEST_ASSERT_TRUE(e.switchApp("{\"name\":\"Silent\",\"fast\":true}"));
  TEST_ASSERT_EQUAL_STRING("Silent", e.currentAppId().c_str());
}

static void test_script_app_duration_overrides_global() {
  struct FScripts : IScriptService {
    DispatchResult setScript(const std::string&, const std::string&, DispatchDetail&) override {
      return DispatchResult::Ok;
    }
    void removeScript(const std::string&) override {}
    long scriptDurationMs(const std::string& name) override {
      return name == "Quick" ? 2000 : 0;
    }
  } scripts;

  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.setScriptService(&scripts);
  e.execute(cmd(CommandType::SetSettings, "", "{\"transitionDurationMs\":400}"));
  e.syncScriptApp("Quick");
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Quick\",\"Time\"],\"disabled\":[\"Date\",\"Temperature\",\"Humidity\",\"Battery\"]}"));
  e.execute(cmd(CommandType::SwitchApp, "", "{\"name\":\"Quick\",\"fast\":true}"));
  e.tick(0);
  TEST_ASSERT_EQUAL_STRING("Quick", e.currentAppId().c_str());
  e.tick(1500);
  TEST_ASSERT_EQUAL_STRING("Quick", e.currentAppId().c_str());
  TEST_ASSERT_FALSE(e.appHost().inTransition());
  e.tick(2000);
  TEST_ASSERT_TRUE(e.appHost().inTransition());
  e.tick(2500);
  TEST_ASSERT_EQUAL_STRING("Time", e.currentAppId().c_str());
}

static void test_script_pause_holds_rotation_until_a_manual_move() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetSettings, "", "{\"transitionDurationMs\":400}"));
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\",\"Date\"],\"disabled\":[\"Temperature\",\"Humidity\",\"Battery\"]}"));
  e.tick(0);
  TEST_ASSERT_EQUAL_STRING("Time", e.currentAppId().c_str());

  e.setScriptRotationPaused(true);
  e.tick(8000);
  TEST_ASSERT_FALSE(e.appHost().inTransition());
  TEST_ASSERT_EQUAL_STRING("Time", e.currentAppId().c_str());

  e.scriptNextApp();
  e.tick(8100);
  e.tick(8600);
  TEST_ASSERT_EQUAL_STRING("Date", e.currentAppId().c_str());
  TEST_ASSERT_TRUE(e.scriptRotationPaused());
  e.tick(20000);
  TEST_ASSERT_FALSE(e.appHost().inTransition());

  e.nextApp();
  TEST_ASSERT_FALSE(e.scriptRotationPaused());
  e.tick(20100);
  e.tick(20600);
  TEST_ASSERT_EQUAL_STRING("Time", e.currentAppId().c_str());

  e.tick(27600);
  TEST_ASSERT_TRUE(e.appHost().inTransition());
}

static void test_settings_parse_error() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ParseError),
                        rc(e.execute(cmd(CommandType::SetSettings, "", "{bad"))));
}

static void test_lifetime_expiry() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.tick(1000);
  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SetPushedApp, "temp",
                       "{\"text\":\"x\",\"lifetimeMs\":2000,\"lifetimeExpiry\":\"remove\"}"))));
  TEST_ASSERT_NOT_NULL(e.pushedApp("temp"));
  e.tick(2500);
  TEST_ASSERT_NOT_NULL(e.pushedApp("temp"));
  e.tick(3500);
  TEST_ASSERT_NULL(e.pushedApp("temp"));
}

static void test_expired_array_child_leaves_siblings_alone() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.tick(1000);
  e.execute(cmd(CommandType::SetPushedApp, "arr",
                "[{\"text\":\"a\",\"lifetimeMs\":2000,\"lifetimeExpiry\":\"remove\"},"
                " {\"text\":\"b\"}]"));
  TEST_ASSERT_NOT_NULL(e.pushedApp("arr0"));
  TEST_ASSERT_NOT_NULL(e.pushedApp("arr1"));

  e.tick(3500);
  TEST_ASSERT_NULL(e.pushedApp("arr0"));
  TEST_ASSERT_NOT_NULL(e.pushedApp("arr1"));
  TEST_ASSERT_TRUE(e.isInLoop("arr1"));
}

static void test_pushed_apps_do_not_persist() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SetPushedApp, "motd", "{\"text\":\"x\"}"))));
  const AppSpec* sp = e.pushedApp("motd");
  TEST_ASSERT_NOT_NULL(sp);
  TEST_ASSERT_EQUAL_STRING("x", sp->text.c_str());
}

static void test_lifetime_mode1_marks_stale() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.tick(1000);
  e.execute(cmd(CommandType::SetPushedApp, "keep",
                "{\"text\":\"x\",\"lifetimeMs\":1000,\"lifetimeExpiry\":\"mark\"}"));
  TEST_ASSERT_FALSE(e.pushedApp("keep")->lifeTimeEnd);
  e.tick(5000);
  TEST_ASSERT_NOT_NULL(e.pushedApp("keep"));
  TEST_ASSERT_TRUE(e.pushedApp("keep")->lifeTimeEnd);
}

static void test_script_sources_stay_writable_without_an_interpreter() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  std::map<std::string, std::string> saved;
  std::vector<std::string> removed;
  script::ScriptSourceService svc(
      [&](const std::string& n, const std::string& s) { saved[n] = s; },
      [&](const std::string& n) { removed.push_back(n); });
  e.setScriptService(&svc);

  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
                        rc(e.execute(cmd(CommandType::ScriptSet, "Broken", "def draw() end"))));
  TEST_ASSERT_EQUAL_STRING("def draw() end", saved["Broken"].c_str());

  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
                        rc(e.execute(cmd(CommandType::ScriptRemove, "Broken"))));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)removed.size());
  TEST_ASSERT_EQUAL_STRING("Broken", removed[0].c_str());

  TEST_ASSERT_EQUAL_UINT(5u, (unsigned)e.appHost().count());
}

static void test_apps_json_lists_stored_scripts_without_an_interpreter() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  std::vector<script::StoredScript> stored;
  script::StoredScript s;
  s.name = "Broken";
  s.meta.name = "Broken";
  s.meta.desc = "eats the heap";
  s.meta.version = "1.0";
  s.meta.headless = true;
  stored.push_back(s);

  std::string out;
  appendAppsJson(out, e, nullptr, &stored);
  TEST_ASSERT_TRUE(out.find("\"name\":\"Broken\"") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"origin\":\"script\"") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"enabled\":false") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"inLoop\":false") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"headless\":true") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"desc\":\"eats the heap\"") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"icons\":[]") != std::string::npos);
}

static void test_apps_json_carries_the_icons_a_script_names() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  std::vector<script::StoredScript> stored;
  script::StoredScript s;
  s.name = "Weather";
  s.meta.name = "Weather";
  s.meta.icons = "2105, 2106 2105";
  stored.push_back(s);

  std::string out;
  appendAppsJson(out, e, nullptr, &stored);
  TEST_ASSERT_TRUE(out.find("\"icons\":[\"2105\",\"2106\"]") != std::string::npos);
}

namespace {
struct FHeadless : IScriptService {
  std::vector<std::string> headless;
  std::vector<std::string> running;
  DispatchResult setScript(const std::string&, const std::string&, DispatchDetail&) override {
    return DispatchResult::Ok;
  }
  void removeScript(const std::string&) override {}
  bool scriptIsHeadless(const std::string& name) override {
    return std::find(headless.begin(), headless.end(), name) != headless.end();
  }
  void setRunningScripts(const std::vector<std::string>& r) override { running = r; }
  bool runs(const std::string& name) const {
    return std::find(running.begin(), running.end(), name) != running.end();
  }
};

std::string appRowJson(const std::string& json, const std::string& name) {
  const std::size_t at = json.find("\"name\":\"" + name + "\"");
  if (at == std::string::npos) return std::string();
  const std::size_t end = json.find('}', at);
  return json.substr(at, end == std::string::npos ? std::string::npos : end - at);
}
}

static void test_a_headless_script_runs_without_being_drawn() {
  FHeadless scripts;
  scripts.headless.push_back("Doorbell");

  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.setScriptService(&scripts);
  e.syncScriptApp("Doorbell");
  e.syncScriptApp("Weather");

  TEST_ASSERT_FALSE(e.isInLoop("Doorbell"));
  TEST_ASSERT_TRUE(e.isEnabled("Doorbell"));
  TEST_ASSERT_TRUE(scripts.runs("Doorbell"));
  TEST_ASSERT_TRUE(e.isInLoop("Weather"));
}

static void test_the_app_order_switches_a_headless_script_on_and_off() {
  FHeadless scripts;
  scripts.headless.push_back("Doorbell");

  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.setScriptService(&scripts);
  e.syncScriptApp("Doorbell");

  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\",\"Doorbell\"],\"disabled\":[\"Date\",\"Temperature\",\"Humidity\",\"Battery\"]}"));
  TEST_ASSERT_TRUE(e.isEnabled("Doorbell"));
  TEST_ASSERT_TRUE(scripts.runs("Doorbell"));
  TEST_ASSERT_FALSE(e.isInLoop("Doorbell"));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)e.appHost().ids().size());
  TEST_ASSERT_EQUAL_STRING("Time", e.appHost().ids()[0].c_str());

  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\"],\"disabled\":[\"Date\",\"Temperature\",\"Humidity\",\"Battery\",\"Doorbell\"]}"));
  TEST_ASSERT_FALSE(e.isEnabled("Doorbell"));
  TEST_ASSERT_FALSE(scripts.runs("Doorbell"));
}

static void test_clearing_the_headless_flag_puts_the_script_on_the_panel() {
  FHeadless scripts;
  scripts.headless.push_back("Doorbell");

  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.setScriptService(&scripts);
  e.syncScriptApp("Doorbell");
  TEST_ASSERT_FALSE(e.isInLoop("Doorbell"));

  scripts.headless.clear();
  e.syncScriptApp("Doorbell");
  TEST_ASSERT_TRUE(e.isInLoop("Doorbell"));
}

static void test_apps_json_separates_enabled_from_in_loop() {
  FHeadless scripts;
  scripts.headless.push_back("Doorbell");

  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.setScriptService(&scripts);
  e.syncScriptApp("Doorbell");

  std::string out;
  appendAppsJson(out, e, nullptr, nullptr);
  const std::string running = appRowJson(out, "Doorbell");
  TEST_ASSERT_TRUE(running.find("\"enabled\":true") != std::string::npos);
  TEST_ASSERT_TRUE(running.find("\"inLoop\":false") != std::string::npos);
  TEST_ASSERT_TRUE(running.find("\"slot\":null") != std::string::npos);

  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\"],\"disabled\":[\"Date\",\"Temperature\",\"Humidity\",\"Battery\",\"Doorbell\"]}"));
  out.clear();
  appendAppsJson(out, e, nullptr, nullptr);
  const std::string stopped = appRowJson(out, "Doorbell");
  TEST_ASSERT_TRUE(stopped.find("\"enabled\":false") != std::string::npos);
  TEST_ASSERT_TRUE(stopped.find("\"inLoop\":false") != std::string::npos);
}

static void test_a_disabled_pushed_app_stays_disabled_across_a_reboot() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  std::string persisted;
  CoreEngine e(so, di, sy);
  e.setOrderPersist([&](const std::string& j) { persisted = j; });
  e.execute(cmd(CommandType::SetPushedApp, "wetter", "{\"text\":\"x\"}"));
  e.execute(cmd(CommandType::SetAppOrder, "",
                "{\"order\":[\"Time\",\"Date\",\"Temperature\",\"Humidity\",\"Battery\"],\"disabled\":[\"wetter\"]}"));
  TEST_ASSERT_FALSE(e.isEnabled("wetter"));

  CoreEngine boot(so, di, sy);
  boot.execute(cmd(CommandType::SetAppOrder, "", persisted));
  boot.execute(cmd(CommandType::SetPushedApp, "wetter", "{\"text\":\"x\"}"));
  TEST_ASSERT_FALSE(boot.isEnabled("wetter"));
  TEST_ASSERT_FALSE(boot.isInLoop("wetter"));
}

static void test_an_explicit_disabled_list_leaves_unmentioned_apps_alone() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\"],\"disabled\":[\"Date\"]}"));
  TEST_ASSERT_FALSE(e.isEnabled("Date"));
  TEST_ASSERT_TRUE(e.isEnabled("Temperature"));
  TEST_ASSERT_TRUE(e.isEnabled("Humidity"));
  TEST_ASSERT_TRUE(e.isEnabled("Battery"));
  TEST_ASSERT_EQUAL_UINT(4u, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_STRING("Time", e.appHost().ids()[0].c_str());
}

static void test_the_apps_inventory_keeps_a_disabled_app_that_is_gone() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetPushedApp, "wetter", "{\"text\":\"x\"}"));
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\"],\"disabled\":[\"wetter\"]}"));
  e.execute(cmd(CommandType::SetPushedApp, "wetter", "", 0, true));
  TEST_ASSERT_NULL(e.pushedApp("wetter"));

  std::string out;
  appendAppsJson(out, e, nullptr, nullptr);
  const std::string row = appRowJson(out, "wetter");
  TEST_ASSERT_TRUE(row.find("\"enabled\":false") != std::string::npos);
  TEST_ASSERT_TRUE(row.find("\"present\":false") != std::string::npos);
}

static void test_deleting_an_app_leaves_unrelated_digit_suffixed_apps_alone() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetPushedApp, "temp", "{\"text\":\"a\"}"));
  e.execute(cmd(CommandType::SetPushedApp, "temp1", "{\"text\":\"b\"}"));
  e.execute(cmd(CommandType::SetPushedApp, "temp2", "{\"text\":\"c\"}"));
  e.execute(cmd(CommandType::SetPushedApp, "temp", "", 0, true));
  TEST_ASSERT_NULL(e.pushedApp("temp"));
  TEST_ASSERT_NOT_NULL(e.pushedApp("temp1"));
  TEST_ASSERT_NOT_NULL(e.pushedApp("temp2"));
}

static void test_disabled_alone_switches_off_without_resending_the_order() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Battery\",\"Time\",\"Date\"],\"disabled\":[\"Temperature\",\"Humidity\"]}"));
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)e.appHost().count());

  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok),
                        rc(e.execute(cmd(CommandType::SetAppOrder, "", "{\"disabled\":[\"Date\"]}"))));
  TEST_ASSERT_FALSE(e.isEnabled("Date"));
  TEST_ASSERT_TRUE(e.isEnabled("Temperature"));
  TEST_ASSERT_EQUAL_STRING("Battery", e.appHost().ids()[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Time", e.appHost().ids()[1].c_str());
  TEST_ASSERT_EQUAL_UINT(4u, (unsigned)e.appHost().count());
}

static void test_an_object_body_with_neither_key_is_rejected() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ParseError),
                        rc(e.execute(cmd(CommandType::SetAppOrder, "", "{\"nope\":[]}"))));
  TEST_ASSERT_EQUAL_UINT(5u, (unsigned)e.appHost().count());
}

static void test_a_reserved_slot_is_listed_while_its_app_is_away() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\",\"wetter\"],\"disabled\":[]}"));
  TEST_ASSERT_FALSE(e.isInLoop("wetter"));
  TEST_ASSERT_TRUE(e.isEnabled("wetter"));

  std::string out;
  appendAppsJson(out, e, nullptr, nullptr);
  const std::string row = appRowJson(out, "wetter");
  TEST_ASSERT_TRUE(row.find("\"enabled\":true") != std::string::npos);
  TEST_ASSERT_TRUE(row.find("\"inLoop\":false") != std::string::npos);
  TEST_ASSERT_TRUE(row.find("\"present\":false") != std::string::npos);

  e.execute(cmd(CommandType::SetPushedApp, "wetter", "{\"text\":\"21\"}"));
  TEST_ASSERT_EQUAL_STRING("wetter", e.appHost().ids()[1].c_str());
}

static void test_deleting_a_script_takes_its_name_out_of_the_arrangement() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  std::string persisted;
  e.setOrderPersist([&](const std::string& j) { persisted = j; });
  e.syncScriptApp("Nightmode");
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\",\"Nightmode\"],\"disabled\":[]}"));
  TEST_ASSERT_TRUE(e.isInLoop("Nightmode"));

  e.removeScriptApp("Nightmode");
  const auto all = e.allApps();
  TEST_ASSERT_TRUE(std::find(all.begin(), all.end(), "Nightmode") == all.end());
  TEST_ASSERT_TRUE(persisted.find("Nightmode") == std::string::npos);
}

static void test_a_deleted_pushed_app_keeps_the_slot_it_was_given() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetPushedApp, "wetter", "{\"text\":\"21\"}"));
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\",\"wetter\",\"Date\"],\"disabled\":[]}"));
  e.execute(cmd(CommandType::SetPushedApp, "wetter", "", 0, true));

  const auto all = e.allApps();
  TEST_ASSERT_TRUE(std::find(all.begin(), all.end(), "wetter") != all.end());
  e.execute(cmd(CommandType::SetPushedApp, "wetter", "{\"text\":\"21\"}"));
  TEST_ASSERT_EQUAL_STRING("wetter", e.appHost().ids()[1].c_str());
}

static void test_an_arranged_app_reports_its_slot_while_it_is_away() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetAppOrder, "", "{\"order\":[\"Time\",\"wetter\",\"Date\"],\"disabled\":[]}"));

  std::string out;
  appendAppsJson(out, e, nullptr, nullptr);
  TEST_ASSERT_TRUE(appRowJson(out, "Time").find("\"slot\":0") != std::string::npos);
  TEST_ASSERT_TRUE(appRowJson(out, "wetter").find("\"slot\":1") != std::string::npos);
  TEST_ASSERT_TRUE(appRowJson(out, "Date").find("\"slot\":2") != std::string::npos);
  TEST_ASSERT_TRUE(appRowJson(out, "Humidity").find("\"slot\":null") != std::string::npos);

  TEST_ASSERT_TRUE(appRowJson(out, "wetter").find("\"inLoop\":false") != std::string::npos);
  TEST_ASSERT_TRUE(appRowJson(out, "Date").find("\"inLoop\":true") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"position\"") == std::string::npos);
}

static void test_a_body_the_engine_cannot_read_leaves_the_arrangement_alone() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  e.execute(cmd(CommandType::SetAppOrder, "",
                "{\"order\":[\"Battery\",\"Time\"],\"disabled\":[\"Date\"]}"));
  TEST_ASSERT_EQUAL_STRING("Battery", e.appHost().ids()[0].c_str());

  const char* bad[] = {
      "{\"order\":[\"Time\"]}",
      "[\"Time\",\"Date\"]",
      "{\"order\":[{\"name\":\"Time\",\"show\":false}],\"disabled\":[]}",
      "{\"order\":\"Time\",\"disabled\":[]}",
      "{\"nope\":[]}",
  };
  for (const char* body : bad) {
    TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ParseError),
                          rc(e.execute(cmd(CommandType::SetAppOrder, "", body))));
    TEST_ASSERT_EQUAL_STRING("Battery", e.appHost().ids()[0].c_str());
    TEST_ASSERT_FALSE(e.isEnabled("Date"));
  }
}

static void test_nothing_is_switched_off_unless_it_is_named() {
  sound::AudioRouter so; FDisplay di; FSystem sy;
  CoreEngine e(so, di, sy);
  TEST_ASSERT_EQUAL_INT(
      rc(DispatchResult::Ok),
      rc(e.execute(cmd(CommandType::SetAppOrder, "",
                       "{\"order\":[\"Humidity\",\"Time\"],\"disabled\":[]}"))));
  TEST_ASSERT_EQUAL_UINT(5u, (unsigned)e.appHost().count());
  TEST_ASSERT_EQUAL_STRING("Humidity", e.appHost().ids()[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Time", e.appHost().ids()[1].c_str());
  TEST_ASSERT_TRUE(e.isEnabled("Date"));
  TEST_ASSERT_TRUE(e.isEnabled("Temperature"));
  TEST_ASSERT_TRUE(e.isEnabled("Battery"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_body_the_engine_cannot_read_leaves_the_arrangement_alone);
  RUN_TEST(test_nothing_is_switched_off_unless_it_is_named);
  RUN_TEST(test_an_arranged_app_reports_its_slot_while_it_is_away);
  RUN_TEST(test_deleting_a_script_takes_its_name_out_of_the_arrangement);
  RUN_TEST(test_a_deleted_pushed_app_keeps_the_slot_it_was_given);
  RUN_TEST(test_a_reserved_slot_is_listed_while_its_app_is_away);
  RUN_TEST(test_disabled_alone_switches_off_without_resending_the_order);
  RUN_TEST(test_an_object_body_with_neither_key_is_rejected);
  RUN_TEST(test_a_disabled_pushed_app_stays_disabled_across_a_reboot);
  RUN_TEST(test_an_explicit_disabled_list_leaves_unmentioned_apps_alone);
  RUN_TEST(test_the_apps_inventory_keeps_a_disabled_app_that_is_gone);
  RUN_TEST(test_deleting_an_app_leaves_unrelated_digit_suffixed_apps_alone);
  RUN_TEST(test_default_app_list);
  RUN_TEST(test_lifetime_expiry);
  RUN_TEST(test_expired_array_child_leaves_siblings_alone);
  RUN_TEST(test_pushed_apps_do_not_persist);
  RUN_TEST(test_lifetime_mode1_marks_stale);
  RUN_TEST(test_custom_app_add_and_delete);
  RUN_TEST(test_array_custom_apps_indexed);
  RUN_TEST(test_pushed_apps_follow_the_order_they_arrived_in);
  RUN_TEST(test_updating_a_pushed_app_leaves_its_place_alone);
  RUN_TEST(test_a_returning_pushed_app_lands_at_the_end);
  RUN_TEST(test_an_array_push_keeps_its_element_order);
  RUN_TEST(test_an_arranged_app_keeps_its_slot_when_others_arrive);
  RUN_TEST(test_custom_app_capacity_is_507);
  RUN_TEST(test_notification_capacity_is_507);
  RUN_TEST(test_per_app_duration_overrides_global);
  RUN_TEST(test_frequent_pushes_do_not_freeze_the_rotation);
  RUN_TEST(test_unknown_effect_or_overlay_name_is_rejected);
  RUN_TEST(test_humidity_app_gated_on_sensor_capability);
  RUN_TEST(test_temperature_app_gated_on_sensor_capability);
  RUN_TEST(test_notify_and_dismiss);
  RUN_TEST(test_bad_scroll_in_a_payload_is_rejected);
  RUN_TEST(test_notification_array_is_rejected_not_truncated);
  RUN_TEST(test_switch_app);
  RUN_TEST(test_the_disabled_list_decides_what_stays_out);
  RUN_TEST(test_an_app_that_turns_up_later_joins_the_loop);
  RUN_TEST(test_a_pushed_app_replaces_the_builtin_of_the_same_name);
  RUN_TEST(test_order_allows_duplicate_apps);
  RUN_TEST(test_order_reserves_spot_for_unknown_apps);
  RUN_TEST(test_apps_inventory_includes_switched_off_apps);
  RUN_TEST(test_settings_change_leaves_loop_alone);
  RUN_TEST(test_bus_submit_then_tick_drains);
  RUN_TEST(test_auto_rotation);
  RUN_TEST(test_script_app_declining_is_skipped_by_the_rotation);
  RUN_TEST(test_script_app_duration_overrides_global);
  RUN_TEST(test_script_pause_holds_rotation_until_a_manual_move);
  RUN_TEST(test_settings_parse_error);
  RUN_TEST(test_script_sources_stay_writable_without_an_interpreter);
  RUN_TEST(test_apps_json_lists_stored_scripts_without_an_interpreter);
  RUN_TEST(test_apps_json_carries_the_icons_a_script_names);
  RUN_TEST(test_a_headless_script_runs_without_being_drawn);
  RUN_TEST(test_the_app_order_switches_a_headless_script_on_and_off);
  RUN_TEST(test_clearing_the_headless_flag_puts_the_script_on_the_panel);
  RUN_TEST(test_apps_json_separates_enabled_from_in_loop);
  return UNITY_END();
}
