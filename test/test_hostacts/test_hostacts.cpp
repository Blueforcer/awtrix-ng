#include <unity.h>

#include <string>
#include <vector>

#include "../EngineFakes.h"
#include "core/CoreEngine.h"
#include "core/apps/AppRegistry.h"
#include "core/remote/HostActs.h"
#include "core/remote/RemoteApps.h"
#include "core/remote/RemoteFrames.h"

using namespace awtrix;
using namespace awtrix::remote;

namespace {

struct Rig {
  sound::AudioRouter audio;
  test::NullDisplay display;
  test::NullSystem system;
  CoreEngine engine{audio, display, system, {"Time", "Date"}};
  AppRegistry registry;
  RemoteFrames frames;
  RemoteApps apps{registry, frames, [this](const std::string& id) { engine.syncScriptApp(id); },
                  [this](const std::string& id) { engine.removeScriptApp(id); },
                  [this](bool hold) { engine.setScriptRotationPaused(hold); }};
  HostActs acts{engine, apps};
  std::vector<int> pressed;

  Rig() {
    frames.begin(32, 8);
    engine.setScriptService(&apps);
    apps.setLinkUp(true);
    wire::Apps list;
    list.revision = 1;
    list.entries[0].name = "Game";
    list.entries[0].flags = wire::app::kWantsShow | wire::app::kTakesButtons;
    list.entries[1].name = "Doom";
    list.entries[1].flags = wire::app::kOnDemand;
    list.count = 2;
    apps.apply(list);
    acts.setPress([this](int button) { pressed.push_back(button); });
    engine.tick(0);
  }

  api::HttpResult act(const std::string& body) { return acts.request("POST", true, body); }
};

bool has(const api::HttpResult& result, const char* text) { return result.body.find(text) != std::string::npos; }

}

void setUp() {}
void tearDown() {}

static void test_only_a_post_with_the_key_is_taken() {
  Rig r;
  api::HttpResult result = r.acts.request("POST", false, "{\"act\":\"next\"}");
  TEST_ASSERT_EQUAL(401, result.status);
  TEST_ASSERT_TRUE(r.engine.incomingAppId().empty());
  result = r.acts.request("PUT", true, "{\"act\":\"next\"}");
  TEST_ASSERT_EQUAL(405, result.status);
  TEST_ASSERT_TRUE(has(result, "allowed: POST"));
  TEST_ASSERT_TRUE(r.engine.incomingAppId().empty());
}

static void test_a_body_that_names_no_known_act_is_refused() {
  Rig r;
  TEST_ASSERT_EQUAL(400, r.act("{").status);
  api::HttpResult result = r.act("{\"act\":\"dance\"}");
  TEST_ASSERT_EQUAL(422, result.status);
  TEST_ASSERT_TRUE(has(result, "invalid act"));
  TEST_ASSERT_EQUAL(422, r.act("{\"hold\":true}").status);
}

static void test_next_and_previous_move_the_rotation() {
  Rig r;
  const std::string first = r.engine.currentAppId();
  api::HttpResult result = r.act("{\"act\":\"next\"}");
  TEST_ASSERT_EQUAL(200, result.status);
  TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", result.body.c_str());
  TEST_ASSERT_FALSE(r.engine.incomingAppId().empty());
  Rig back;
  TEST_ASSERT_EQUAL(200, back.act("{\"act\":\"previous\"}").status);
  TEST_ASSERT_FALSE(back.engine.incomingAppId().empty());
  TEST_ASSERT_TRUE(back.engine.incomingAppId() != r.engine.incomingAppId());
  TEST_ASSERT_EQUAL_STRING(first.c_str(), r.engine.currentAppId().c_str());
}

static void test_show_switches_to_a_known_app() {
  Rig r;
  TEST_ASSERT_EQUAL(200, r.act("{\"act\":\"show\",\"app\":\"Game\"}").status);
  TEST_ASSERT_EQUAL_STRING("Game", r.engine.incomingAppId().c_str());
  api::HttpResult result = r.act("{\"act\":\"show\",\"app\":\"Nope\"}");
  TEST_ASSERT_EQUAL(404, result.status);
  TEST_ASSERT_EQUAL(422, r.act("{\"act\":\"show\"}").status);
}

static void test_a_hold_lasts_until_released_or_the_link_goes() {
  Rig r;
  TEST_ASSERT_EQUAL(200, r.act("{\"act\":\"hold\",\"hold\":true}").status);
  TEST_ASSERT_TRUE(r.engine.scriptRotationPaused());
  TEST_ASSERT_EQUAL(200, r.act("{\"act\":\"hold\",\"hold\":false}").status);
  TEST_ASSERT_FALSE(r.engine.scriptRotationPaused());
  r.act("{\"act\":\"hold\",\"hold\":true}");
  r.engine.nextApp();
  TEST_ASSERT_FALSE_MESSAGE(r.engine.scriptRotationPaused(), "the user moving on lets go");
  r.act("{\"act\":\"hold\",\"hold\":true}");
  TEST_ASSERT_TRUE_MESSAGE(r.engine.scriptRotationPaused(), "and the host may hold again");
  r.apps.setLinkUp(false);
  TEST_ASSERT_FALSE_MESSAGE(r.engine.scriptRotationPaused(), "a host that went away holds nothing");
  r.act("{\"act\":\"hold\",\"hold\":true}");
  TEST_ASSERT_FALSE(r.engine.scriptRotationPaused());
  TEST_ASSERT_EQUAL(422, r.act("{\"act\":\"hold\",\"hold\":\"yes\"}").status);
}

static void test_a_restart_counts_only_for_the_app_on_the_display() {
  Rig shown;
  Rig other;
  const std::string current = shown.engine.currentAppId();
  shown.engine.tick(6900);
  other.engine.tick(6900);
  TEST_ASSERT_EQUAL(200, shown.act("{\"act\":\"restart\",\"app\":\"" + current + "\"}").status);
  TEST_ASSERT_EQUAL(200, other.act("{\"act\":\"restart\",\"app\":\"Game\"}").status);
  shown.engine.tick(7100);
  other.engine.tick(7100);
  TEST_ASSERT_TRUE_MESSAGE(shown.engine.incomingAppId().empty(), "the turn started over");
  TEST_ASSERT_FALSE_MESSAGE(other.engine.incomingAppId().empty(), "an app not shown has no turn to restart");
}

static void test_a_press_the_host_app_did_not_use_does_its_default() {
  Rig r;
  const std::string current = r.engine.currentAppId();
  TEST_ASSERT_EQUAL(200, r.act("{\"act\":\"press\",\"app\":\"" + current + "\",\"button\":2}").status);
  TEST_ASSERT_EQUAL(1, r.pressed.size());
  TEST_ASSERT_EQUAL(2, r.pressed[0]);
  TEST_ASSERT_EQUAL(200, r.act("{\"act\":\"press\",\"app\":\"Game\",\"button\":0}").status);
  TEST_ASSERT_EQUAL_MESSAGE(1, r.pressed.size(), "a press for an app gone from the display is dropped");
  TEST_ASSERT_EQUAL(422, r.act("{\"act\":\"press\",\"app\":\"" + current + "\",\"button\":3}").status);
  TEST_ASSERT_EQUAL(422, r.act("{\"act\":\"press\",\"app\":\"" + current + "\"}").status);
  TEST_ASSERT_EQUAL(1, r.pressed.size());
}

static void test_a_session_ends_when_its_app_asks() {
  Rig r;
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(static_cast<int>(DispatchResult::Ok), static_cast<int>(r.engine.startSession("Doom", detail)));
  TEST_ASSERT_EQUAL(404, r.act("{\"act\":\"endSession\",\"id\":\"Game\"}").status);
  TEST_ASSERT_EQUAL(200, r.act("{\"act\":\"endSession\",\"id\":\"Doom\"}").status);
  r.engine.tick(100);
  TEST_ASSERT_FALSE(r.engine.inSession());
}

static void test_sounds_run_as_the_scripts_own() {
  Rig r;
  TEST_ASSERT_EQUAL(200, r.act("{\"act\":\"sound\",\"script\":\"Game\",\"action\":\"stop\"}").status);
  TEST_ASSERT_EQUAL(200, r.act("{\"act\":\"sound\",\"script\":\"Game\",\"action\":\"release\"}").status);
  api::HttpResult result = r.act("{\"act\":\"sound\",\"script\":\"Game\",\"action\":\"loud\"}");
  TEST_ASSERT_EQUAL(422, result.status);
  TEST_ASSERT_TRUE(has(result, "\"field\":\"action\""));
  result = r.act("{\"act\":\"sound\",\"action\":\"stop\"}");
  TEST_ASSERT_EQUAL(422, result.status);
  TEST_ASSERT_TRUE(has(result, "\"field\":\"script\""));
  result = r.act("{\"act\":\"sound\",\"script\":\"Game\",\"action\":\"play\",\"sound\":{\"mp3\":\"x\"}}");
  TEST_ASSERT_EQUAL(422, result.status);
  TEST_ASSERT_TRUE(has(result, "\"field\":\"mp3\""));
}

static void test_a_notification_is_the_scripts_own() {
  Rig r;
  TEST_ASSERT_EQUAL(200, r.act("{\"act\":\"notify\",\"script\":\"Game\",\"notification\":{\"text\":\"hi\"}}").status);
  r.engine.tick(10);
  TEST_ASSERT_TRUE(r.engine.hasNotification());
  TEST_ASSERT_GREATER_OR_EQUAL(400, r.act("{\"act\":\"notify\",\"script\":\"Game\"}").status);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_only_a_post_with_the_key_is_taken);
  RUN_TEST(test_a_body_that_names_no_known_act_is_refused);
  RUN_TEST(test_next_and_previous_move_the_rotation);
  RUN_TEST(test_show_switches_to_a_known_app);
  RUN_TEST(test_a_hold_lasts_until_released_or_the_link_goes);
  RUN_TEST(test_a_restart_counts_only_for_the_app_on_the_display);
  RUN_TEST(test_a_press_the_host_app_did_not_use_does_its_default);
  RUN_TEST(test_a_session_ends_when_its_app_asks);
  RUN_TEST(test_sounds_run_as_the_scripts_own);
  RUN_TEST(test_a_notification_is_the_scripts_own);
  return UNITY_END();
}
