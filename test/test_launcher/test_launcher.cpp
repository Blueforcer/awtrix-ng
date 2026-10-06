#include "../EngineFakes.h"
#include "core/sound/RoutedPcmSink.h"
#include <unity.h>
#include "core/script/EngineScriptApplication.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "core/CoreEngine.h"
#include "core/api/StateJson.h"
#include "core/apps/AppRegistry.h"
#include "core/input/ButtonRouter.h"
#include "core/launcher/Launcher.h"
#include "core/script/ScriptHost.h"
#include "core/script/ScriptService.h"

using namespace awtrix;

namespace {

using FDisplay = awtrix::test::NullDisplay;
using FSystem = awtrix::test::NullSystem;

struct FPcm : sound::RoutedPcmSink {
  void setVolumes(const sound::Volumes&) override {}
  bool playMp3(const std::string&, sound::Group) override { return true; }
  void stopOneShot() override {}
  bool oneShotPlaying() const override { return false; }
  DispatchResult playStream(const std::string&, const std::string&, DispatchDetail&) override {
    return DispatchResult::Ok;
  }
  void stopStream() override {}
  void tick(int64_t) override {}
};

struct FScripts : IScriptService {
  std::set<std::string> onDemand;
  std::set<std::string> loaded;
  std::vector<std::string> running;
  std::vector<std::string> log;
  DispatchResult launchResult = DispatchResult::Ok;

  DispatchResult setScript(const std::string&, const std::string&, DispatchDetail&) override {
    return DispatchResult::Ok;
  }
  void removeScript(const std::string&) override {}
  void setRunningScripts(const std::vector<std::string>& r) override { running = r; }
  bool scriptIsOnDemand(const std::string& name) override { return onDemand.count(name) != 0; }
  DispatchResult launchScript(const std::string& name, DispatchDetail&) override {
    log.push_back("launch:" + name);
    if (launchResult == DispatchResult::Ok) loaded.insert(name);
    return launchResult;
  }
  void unloadScript(const std::string& name) override {
    log.push_back("unload:" + name);
    loaded.erase(name);
  }
  std::string scriptTitle(const std::string& name) override { return "Title " + name; }
};

// An engine with Time and Date in the loop, one rotation script and the on-demand ones.
struct Rig {
  sound::AudioRouter audio;
  FDisplay display;
  FSystem system;
  FPcm pcm;
  FScripts scripts;
  CoreEngine engine{audio, display, system, {"Time", "Date"}};

  explicit Rig(std::vector<std::string> games = {"Doom", "Racer"}) {
    engine.setScriptService(&scripts);
    engine.syncScriptApp("Clock2");
    for (const auto& g : games) {
      scripts.onDemand.insert(g);
      engine.syncScriptApp(g);
    }
    engine.tick(0);
  }

  void withRadio(const char* stations) {
    audio.setPcm(&pcm);
    engine.setPcmSink(&pcm);
    DispatchDetail detail;
    engine.setStations(stations, detail);
  }

  bool inLoop(const std::string& id) const { return engine.isInLoop(id); }
  bool running(const std::string& id) const {
    return std::find(scripts.running.begin(), scripts.running.end(), id) != scripts.running.end();
  }
};

struct FMenu : input::IButtonMenu {
  bool opened = false;
  std::vector<std::string> log;
  bool isOpen() const override { return opened; }
  void open(int64_t) override { opened = true; log.push_back("open"); }
  void close(int64_t) override { opened = false; log.push_back("close"); }
  void step(int d, int64_t) override { log.push_back(d < 0 ? "prev" : "next"); }
  void confirm(int64_t) override { log.push_back("confirm"); }
};

std::string joined(const std::vector<std::string>& v) {
  std::string out;
  for (const auto& s : v) out += s + ",";
  return out;
}

ButtonState pressed(bool left, bool select, bool right) { return {left, select, right}; }

}

void setUp() {}
void tearDown() {}

// --- sessions ---------------------------------------------------------------------------------

static void test_on_demand_scripts_stay_out_of_the_rotation() {
  Rig r;
  TEST_ASSERT_FALSE(r.inLoop("Doom"));
  TEST_ASSERT_FALSE(r.running("Doom"));
  TEST_ASSERT_TRUE(r.inLoop("Clock2"));
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)r.engine.onDemandApps().size());
  TEST_ASSERT_TRUE(r.engine.isScriptApp("Doom"));
}

static void test_a_session_has_the_panel_to_itself_until_it_ends() {
  Rig r;
  r.engine.appHost().switchTo("Date", 0);
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(DispatchResult::Ok, r.engine.startSession("Doom", detail));
  TEST_ASSERT_EQUAL_STRING("Doom", r.engine.currentAppId().c_str());
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)r.engine.appHost().count());
  TEST_ASSERT_TRUE(r.running("Doom"));
  TEST_ASSERT_TRUE(r.running("Clock2"));

  r.engine.tick(600000);
  TEST_ASSERT_EQUAL_STRING("Doom", r.engine.currentAppId().c_str());

  r.engine.endSession();
  TEST_ASSERT_FALSE(r.engine.inSession());
  TEST_ASSERT_EQUAL_STRING("launch:Doom,unload:Doom,", joined(r.scripts.log).c_str());
  TEST_ASSERT_EQUAL_STRING("Date", r.engine.currentAppId().c_str());
  TEST_ASSERT_FALSE(r.running("Doom"));
  TEST_ASSERT_FALSE(r.inLoop("Doom"));
}

static void test_a_refused_start_leaves_the_rotation_alone() {
  Rig r;
  r.scripts.launchResult = DispatchResult::Capacity;
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(DispatchResult::Capacity, r.engine.startSession("Doom", detail));
  TEST_ASSERT_FALSE(r.engine.inSession());
  TEST_ASSERT_EQUAL_STRING("Time", r.engine.currentAppId().c_str());
  TEST_ASSERT_EQUAL(DispatchResult::NotFound, r.engine.startSession("Clock2", detail));
}

static void test_starting_another_session_unloads_the_first() {
  Rig r;
  DispatchDetail detail;
  r.engine.startSession("Doom", detail);
  r.engine.startSession("Racer", detail);
  TEST_ASSERT_EQUAL_STRING("launch:Doom,unload:Doom,launch:Racer,", joined(r.scripts.log).c_str());
  TEST_ASSERT_EQUAL_STRING("Racer", r.engine.currentAppId().c_str());
}

static void test_the_api_starts_and_ends_sessions() {
  Rig r;
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(DispatchResult::Ok, r.engine.switchApp("Doom", detail));
  TEST_ASSERT_EQUAL_STRING("Doom", r.engine.sessionApp().c_str());
  TEST_ASSERT_EQUAL(DispatchResult::Ok, r.engine.switchApp("{\"name\":\"Date\",\"fast\":true}", detail));
  TEST_ASSERT_FALSE(r.engine.inSession());
  TEST_ASSERT_EQUAL_STRING("Date", r.engine.currentAppId().c_str());

  r.engine.switchApp("Doom", detail);
  r.engine.nextApp();
  TEST_ASSERT_FALSE(r.engine.inSession());
  TEST_ASSERT_EQUAL(DispatchResult::NotFound, r.engine.switchApp("Nope", detail));
}

static void test_deleting_the_running_script_ends_its_session() {
  Rig r;
  DispatchDetail detail;
  r.engine.startSession("Doom", detail);
  r.engine.removeScriptApp("Doom");
  TEST_ASSERT_FALSE(r.engine.inSession());
  TEST_ASSERT_EQUAL_STRING("Time", r.engine.currentAppId().c_str());
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)r.engine.onDemandApps().size());
}

static void test_saving_without_ondemand_turns_it_into_a_rotation_app() {
  Rig r;
  DispatchDetail detail;
  r.engine.startSession("Doom", detail);
  r.scripts.onDemand.erase("Doom");
  r.engine.syncScriptApp("Doom");
  TEST_ASSERT_FALSE(r.engine.inSession());
  TEST_ASSERT_TRUE(r.inLoop("Doom"));
  TEST_ASSERT_FALSE(r.engine.isOnDemandApp("Doom"));
}

static void test_a_session_closes_itself_on_the_next_tick() {
  Rig r;
  DispatchDetail detail;
  r.engine.startSession("Doom", detail);
  TEST_ASSERT_FALSE(r.engine.requestSessionEnd("Clock2"));
  TEST_ASSERT_FALSE(r.engine.requestSessionEnd("Racer"));
  TEST_ASSERT_TRUE(r.engine.requestSessionEnd("Doom"));
  TEST_ASSERT_TRUE(r.engine.inSession());
  TEST_ASSERT_EQUAL_STRING("launch:Doom,", joined(r.scripts.log).c_str());

  r.engine.tick(100);
  TEST_ASSERT_FALSE(r.engine.inSession());
  TEST_ASSERT_EQUAL_STRING("launch:Doom,unload:Doom,", joined(r.scripts.log).c_str());
  TEST_ASSERT_EQUAL_STRING("Time", r.engine.currentAppId().c_str());
}

static void test_a_close_request_dies_with_its_session() {
  Rig r;
  DispatchDetail detail;
  r.engine.startSession("Doom", detail);
  r.engine.requestSessionEnd("Doom");
  r.engine.startSession("Racer", detail);
  r.engine.tick(100);
  TEST_ASSERT_EQUAL_STRING("Racer", r.engine.sessionApp().c_str());
  TEST_ASSERT_FALSE(r.engine.requestSessionEnd(""));
}

static void test_a_close_request_dies_when_the_script_joins_the_rotation() {
  Rig r;
  DispatchDetail detail;
  r.engine.startSession("Doom", detail);
  r.engine.requestSessionEnd("Doom");
  r.scripts.onDemand.erase("Doom");
  r.engine.syncScriptApp("Doom");
  r.engine.startSession("Racer", detail);
  r.engine.tick(100);
  TEST_ASSERT_EQUAL_STRING("Racer", r.engine.sessionApp().c_str());
}

// --- button router ----------------------------------------------------------------------------

namespace {
struct RouterRig {
  Rig rig;
  FMenu menu;
  input::ButtonRouter router;
  std::vector<std::string> hook;
  int64_t now = 0;
  bool swapped = false;
  bool scriptTakes = false;

  RouterRig() {
    router.begin(rig.engine, &menu);
    router.setScriptHook([this](int button, bool down, bool) {
      static const char* names[] = {"left", "select", "right"};
      if (down) hook.push_back(names[button]);
      return scriptTakes;
    });
  }
  void at(int64_t ms, ButtonState state) {
    now = ms;
    router.update(state, swapped, now);
  }
  // Holds the state from the current time to until, one update per 25 ms frame.
  void hold(int64_t until, ButtonState state) {
    for (int64_t t = now; t <= until; t += 25) at(t, state);
  }
};
}

static void test_holding_select_opens_the_menu_and_its_release_does_not_confirm() {
  RouterRig r;
  r.hold(475, pressed(false, true, false));
  TEST_ASSERT_FALSE(r.menu.opened);
  r.hold(1500, pressed(false, true, false));
  TEST_ASSERT_TRUE(r.menu.opened);
  r.at(1525, pressed(false, false, false));
  TEST_ASSERT_EQUAL_STRING("open,", joined(r.menu.log).c_str());
}

static void test_the_open_menu_takes_the_buttons() {
  RouterRig r;
  r.hold(1000, pressed(false, true, false));
  r.at(1100, pressed(false, false, false));
  r.hook.clear();
  r.at(1200, pressed(false, false, true));
  r.at(1225, pressed(false, false, false));
  r.at(1300, pressed(true, false, false));
  r.at(1325, pressed(false, false, false));
  r.at(1400, pressed(false, true, false));
  r.at(1500, pressed(false, false, false));
  TEST_ASSERT_EQUAL_STRING("open,next,prev,confirm,", joined(r.menu.log).c_str());
  TEST_ASSERT_EQUAL_STRING("", joined(r.hook).c_str());
  TEST_ASSERT_EQUAL_STRING("Time", r.rig.engine.currentAppId().c_str());

  r.hold(2500, pressed(false, true, false));
  r.at(2525, pressed(false, false, false));
  TEST_ASSERT_EQUAL_STRING("open,next,prev,confirm,close,", joined(r.menu.log).c_str());
}

static void test_held_arrows_keep_stepping_in_the_menu() {
  RouterRig r;
  r.menu.opened = true;
  r.hold(1000, pressed(false, false, true));
  // First step on the press, the next after 400 ms, then every 150 ms: 0, 400, 550, 700, 850, 1000.
  TEST_ASSERT_EQUAL_STRING("next,next,next,next,next,next,", joined(r.menu.log).c_str());
}

static void test_a_script_holding_select_loses_it_to_the_menu() {
  RouterRig r;
  r.scriptTakes = true;
  std::vector<bool> selectSeen;
  r.router.setScriptHook([&](int button, bool down, bool) {
    if (button == input::index(input::Button::Select)) selectSeen.push_back(down);
    return true;
  });
  r.hold(1100, pressed(false, true, false));
  TEST_ASSERT_TRUE(r.menu.opened);
  TEST_ASSERT_TRUE(selectSeen.front());
  TEST_ASSERT_FALSE(selectSeen.back());
}

static void test_arrows_do_not_leave_a_session_and_holding_select_ends_it() {
  RouterRig r;
  DispatchDetail detail;
  r.rig.engine.startSession("Doom", detail);
  r.at(0, pressed(false, false, true));
  r.at(25, pressed(false, false, false));
  r.rig.engine.tick(50);
  TEST_ASSERT_TRUE(r.rig.engine.inSession());
  r.hold(1100, pressed(false, true, false));
  TEST_ASSERT_FALSE(r.rig.engine.inSession());
  TEST_ASSERT_FALSE(r.menu.opened);
}

static void test_arrows_navigate_and_select_dismisses_outside_the_menu() {
  RouterRig r;
  r.at(0, pressed(false, false, true));
  r.at(25, pressed(false, false, false));
  r.rig.engine.tick(50);
  TEST_ASSERT_EQUAL_STRING("Date", r.rig.engine.incomingAppId().c_str());
  TEST_ASSERT_EQUAL_STRING("right,", joined(r.hook).c_str());
}

static void test_swapped_buttons_step_the_other_way() {
  RouterRig r;
  r.swapped = true;
  r.menu.opened = true;
  r.at(0, pressed(true, false, false));
  TEST_ASSERT_EQUAL_STRING("next,", joined(r.menu.log).c_str());
}

static void test_blocked_navigation_or_a_dark_panel_keeps_the_menu_shut() {
  RouterRig r;
  r.rig.engine.execute([] {
    Command c(CommandType::SetSettings);
    c.payload = "{\"blockNavigation\":true}";
    return c;
  }());
  r.hold(1500, pressed(false, true, false));
  TEST_ASSERT_FALSE(r.menu.opened);

  RouterRig dark;
  dark.rig.engine.state().runtime().matrixOff = true;
  dark.hold(1500, pressed(false, true, false));
  TEST_ASSERT_FALSE(dark.menu.opened);
}

static void test_double_press_still_switches_the_panel() {
  RouterRig r;
  r.at(0, pressed(false, true, false));
  r.at(50, pressed(false, false, false));
  r.at(200, pressed(false, true, false));
  TEST_ASSERT_TRUE(r.rig.engine.state().runtime().matrixOff);
}

// --- launcher ---------------------------------------------------------------------------------

static const char* kStations =
    "{\"stations\":[{\"name\":\"SWR3\",\"url\":\"http://a.example/1\"},"
    "{\"name\":\"FFH\",\"url\":\"http://a.example/2\"}]}";

static void test_the_root_lists_what_this_device_can_offer() {
  Rig scriptsOnly;
  launcher::Launcher a(scriptsOnly.engine);
  a.open(0);
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)a.entries().size());
  TEST_ASSERT_EQUAL_STRING("Scripts", a.entries()[0].label.c_str());

  Rig both;
  both.withRadio(kStations);
  launcher::Launcher b(both.engine);
  b.open(0);
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)b.entries().size());

  Rig none({});
  launcher::Launcher c(none.engine);
  c.open(0);
  TEST_ASSERT_TRUE(c.entries().empty());
  TEST_ASSERT_FALSE(c.notice().empty());
  c.tick(launcher::Launcher::kNoticeMs);
  TEST_ASSERT_FALSE(c.isOpen());
}

static void test_picking_a_script_starts_its_session_and_closes() {
  Rig r;
  launcher::Launcher menu(r.engine);
  menu.open(0);
  menu.confirm(10);
  TEST_ASSERT_EQUAL(launcher::Page::Scripts, menu.page());
  TEST_ASSERT_EQUAL_STRING("Title Doom", menu.entries()[0].label.c_str());
  menu.step(1, 20);
  TEST_ASSERT_EQUAL_INT(1, menu.selected());
  menu.step(1, 30);
  TEST_ASSERT_EQUAL_INT(0, menu.selected());
  menu.step(-1, 40);
  menu.confirm(50);
  TEST_ASSERT_FALSE(menu.isOpen());
  TEST_ASSERT_EQUAL_STRING("Racer", r.engine.sessionApp().c_str());
}

static void test_a_script_that_will_not_start_says_so_and_stays() {
  Rig r;
  r.scripts.launchResult = DispatchResult::Capacity;
  launcher::Launcher menu(r.engine);
  menu.open(0);
  menu.confirm(10);
  menu.confirm(20);
  TEST_ASSERT_TRUE(menu.isOpen());
  TEST_ASSERT_FALSE(menu.notice().empty());
  menu.tick(20 + launcher::Launcher::kNoticeMs);
  TEST_ASSERT_TRUE(menu.notice().empty());
  TEST_ASSERT_TRUE(menu.isOpen());
}

static void test_the_radio_page_plays_stops_and_stays_open() {
  Rig r({});
  r.withRadio(kStations);
  launcher::Launcher menu(r.engine);
  menu.open(0);
  menu.confirm(10);
  TEST_ASSERT_EQUAL(launcher::Page::Radio, menu.page());
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)menu.entries().size());
  TEST_ASSERT_EQUAL_STRING("FFH", menu.entries()[0].label.c_str());

  menu.step(1, 20);
  menu.confirm(30);
  TEST_ASSERT_TRUE(menu.isOpen());
  TEST_ASSERT_TRUE(r.engine.state().runtime().radioPlaying);
  TEST_ASSERT_EQUAL_STRING("SWR3", r.engine.state().runtime().radioStation.c_str());
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)menu.entries().size());
  TEST_ASSERT_EQUAL(launcher::Entry::Kind::Stop, menu.entries()[0].kind);
  TEST_ASSERT_EQUAL_STRING("SWR3", menu.entries()[menu.selected()].label.c_str());
  TEST_ASSERT_TRUE(menu.entries()[menu.selected()].playing);

  menu.step(-1, 40);
  menu.step(-1, 50);
  menu.confirm(60);
  TEST_ASSERT_FALSE(r.engine.state().runtime().radioPlaying);
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)menu.entries().size());
  TEST_ASSERT_TRUE(menu.isOpen());
}

static void test_reopening_the_radio_puts_the_cursor_on_the_playing_station() {
  Rig r({});
  r.withRadio(kStations);
  Command play(CommandType::PlayAudio);
  play.payload = "{\"station\":1}";
  r.engine.execute(play);
  launcher::Launcher menu(r.engine);
  menu.open(0);
  menu.confirm(10);
  TEST_ASSERT_EQUAL_STRING("SWR3", menu.entries()[menu.selected()].label.c_str());
}

static void test_an_idle_menu_closes_itself() {
  Rig r;
  launcher::Launcher menu(r.engine);
  menu.open(0);
  menu.step(1, 5000);
  menu.tick(5000 + launcher::Launcher::kIdleCloseMs - 1);
  TEST_ASSERT_TRUE(menu.isOpen());
  menu.tick(5000 + launcher::Launcher::kIdleCloseMs);
  TEST_ASSERT_FALSE(menu.isOpen());
}

// --- end to end with the real script host ----------------------------------------------------

static void test_a_real_ondemand_script_runs_only_in_its_session() {
  const std::string source =
      "# @name Tiny Game\n# @ondemand\nclass App\ndef draw() end\nend\nreturn App()";
  script::ScriptServices services;
  services.readSource = [&](const std::string& name, std::string& out) {
    if (name != "Game") return false;
    out = source;
    return true;
  };
  sound::AudioRouter audio;
  FDisplay display;
  FSystem system;
  CoreEngine engine(audio, display, system);
  AppRegistry registry;
  script::ScriptHost host(registry, services, [&](const std::string& n) { engine.syncScriptApp(n); },
                          [&](const std::string& n) { engine.removeScriptApp(n); });
  script::ScriptService service(host, nullptr, nullptr);
  engine.setScriptService(&service);
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(DispatchResult::Ok, service.setScript("Game", source, detail));

  std::string json;
  appendAppsJson(json, engine, &host, nullptr);
  TEST_ASSERT_TRUE(json.find("\"name\":\"Game\",\"enabled\":true,\"inLoop\":false,\"slot\":null,"
                             "\"present\":true,\"origin\":\"script\"") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"ondemand\":true") != std::string::npos);
  TEST_ASSERT_NULL(registry.find("Game"));

  launcher::Launcher menu(engine);
  menu.open(0);
  menu.confirm(10);
  TEST_ASSERT_EQUAL_STRING("Tiny Game", menu.entries()[0].label.c_str());
  menu.confirm(20);
  TEST_ASSERT_EQUAL_STRING("Game", engine.currentAppId().c_str());
  TEST_ASSERT_NOT_NULL(registry.find("Game"));

  engine.endSession();
  TEST_ASSERT_NULL(registry.find("Game"));
  TEST_ASSERT_EQUAL_STRING("Time", engine.currentAppId().c_str());
}

// The script asks from inside its own draw(): it finishes that call, the next tick unloads it.
static void test_a_real_ondemand_script_closes_itself() {
  const std::string source =
      "# @ondemand\nclass App\ndef draw() pixel(0, 0, rotation.close() ? 0xFF0000 : 0x00FF00) "
      "end\nend\nreturn App()";
  script::ScriptServices services;
  services.readSource = [&](const std::string& name, std::string& out) {
    out = source;
    return name == "Game";
  };
  sound::AudioRouter audio;
  FDisplay display;
  FSystem system;
  CoreEngine engine(audio, display, system);
  script::EngineScriptApplication application(engine, audio);
  services.application = &application;
  AppRegistry registry;
  script::ScriptHost host(registry, services, [&](const std::string& n) { engine.syncScriptApp(n); },
                          [&](const std::string& n) { engine.removeScriptApp(n); });
  script::ScriptService service(host, nullptr, nullptr);
  engine.setScriptService(&service);
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(DispatchResult::Ok, service.setScript("Game", source, detail));
  TEST_ASSERT_EQUAL(DispatchResult::Ok, engine.startSession("Game", detail));

  Canvas canvas(32, 8);
  RenderCtx ctx;
  registry.find("Game")->render(canvas, ctx);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, canvas.getPixel(0, 0));
  TEST_ASSERT_TRUE(engine.inSession());
  TEST_ASSERT_NOT_NULL(registry.find("Game"));

  engine.tick(100);
  TEST_ASSERT_FALSE(engine.inSession());
  TEST_ASSERT_NULL(registry.find("Game"));
}

// A game that cannot run here gives up in setup(), before its first frame.
static void test_a_real_ondemand_script_can_close_from_setup() {
  const std::string source =
      "# @ondemand\nclass App\ndef setup() rotation.close() end\ndef draw() end\nend\n"
      "return App()";
  script::ScriptServices services;
  services.readSource = [&](const std::string& name, std::string& out) {
    out = source;
    return name == "Game";
  };
  sound::AudioRouter audio;
  FDisplay display;
  FSystem system;
  CoreEngine engine(audio, display, system);
  script::EngineScriptApplication application(engine, audio);
  services.application = &application;
  AppRegistry registry;
  script::ScriptHost host(registry, services, [&](const std::string& n) { engine.syncScriptApp(n); },
                          [&](const std::string& n) { engine.removeScriptApp(n); });
  script::ScriptService service(host, nullptr, nullptr);
  engine.setScriptService(&service);
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(DispatchResult::Ok, service.setScript("Game", source, detail));
  TEST_ASSERT_EQUAL(DispatchResult::Ok, engine.startSession("Game", detail));
  engine.tick(100);
  TEST_ASSERT_FALSE(engine.inSession());
  TEST_ASSERT_NULL(registry.find("Game"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_real_ondemand_script_runs_only_in_its_session);
  RUN_TEST(test_on_demand_scripts_stay_out_of_the_rotation);
  RUN_TEST(test_a_session_has_the_panel_to_itself_until_it_ends);
  RUN_TEST(test_a_refused_start_leaves_the_rotation_alone);
  RUN_TEST(test_starting_another_session_unloads_the_first);
  RUN_TEST(test_the_api_starts_and_ends_sessions);
  RUN_TEST(test_deleting_the_running_script_ends_its_session);
  RUN_TEST(test_saving_without_ondemand_turns_it_into_a_rotation_app);
  RUN_TEST(test_a_session_closes_itself_on_the_next_tick);
  RUN_TEST(test_a_close_request_dies_with_its_session);
  RUN_TEST(test_a_real_ondemand_script_closes_itself);
  RUN_TEST(test_a_close_request_dies_when_the_script_joins_the_rotation);
  RUN_TEST(test_a_real_ondemand_script_can_close_from_setup);
  RUN_TEST(test_holding_select_opens_the_menu_and_its_release_does_not_confirm);
  RUN_TEST(test_the_open_menu_takes_the_buttons);
  RUN_TEST(test_held_arrows_keep_stepping_in_the_menu);
  RUN_TEST(test_a_script_holding_select_loses_it_to_the_menu);
  RUN_TEST(test_arrows_do_not_leave_a_session_and_holding_select_ends_it);
  RUN_TEST(test_arrows_navigate_and_select_dismisses_outside_the_menu);
  RUN_TEST(test_swapped_buttons_step_the_other_way);
  RUN_TEST(test_blocked_navigation_or_a_dark_panel_keeps_the_menu_shut);
  RUN_TEST(test_double_press_still_switches_the_panel);
  RUN_TEST(test_the_root_lists_what_this_device_can_offer);
  RUN_TEST(test_picking_a_script_starts_its_session_and_closes);
  RUN_TEST(test_a_script_that_will_not_start_says_so_and_stays);
  RUN_TEST(test_the_radio_page_plays_stops_and_stays_open);
  RUN_TEST(test_reopening_the_radio_puts_the_cursor_on_the_playing_station);
  RUN_TEST(test_an_idle_menu_closes_itself);
  return UNITY_END();
}
