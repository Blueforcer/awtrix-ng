#include "../EngineFakes.h"
#include "core/sound/RoutedPcmSink.h"
#include <unity.h>

#include <memory>
#include "../Visuals.h"

#include "core/ApplicationRuntime.h"
#include "core/ScreenDisplay.h"
#include "core/SettingsSaver.h"
#include "core/script/ScriptSlots.h"
#include "core/script/RestoreScripts.h"
#include "core/apps/DefaultApps.h"
#include "core/apps/builtin/TimeApp.h"
#include "core/script/ScriptService.h"
#include "media/AwtrixFontAdapter.h"

using namespace awtrix;

namespace {
using Display = awtrix::test::NullDisplay;
using System = awtrix::test::NullSystem;
struct Clock : IPageClock {
  void fill(RenderCtx& ctx, int64_t now) override {
    ctx.nowMs = now;
    ctx.hour = 12;
    ctx.minute = 34;
    ctx.second = 0;
    ctx.weekday = 2;
    ctx.mday = 15;
    ctx.month = 9;
    ctx.year = 2026;
  }
};
struct Rig {
  sound::AudioRouter audio;
  Display display;
  System system;
  Clock clock;
  uint8_t brightness = 0;
  DefaultApps defaults;
  ApplicationRuntime runtime;
  explicit Rig(std::function<void(AppRegistry&)> configure = {})
      : runtime(32, 8, {audio, display, system, clock, awtrixFontCatalog(), nullptr, nullptr,
                       [this](uint8_t value) { brightness = value; }}, registry(configure)) {}
  AppRegistry registry(const std::function<void(AppRegistry&)>& configure) {
    auto apps = defaults.registry();
    if (configure) configure(apps);
    return apps;
  }

  void frame(int64_t now) {
    runtime.beginFrame(now);
    runtime.tick(now);
    runtime.render(now);
  }
};

void test_disabled_scripts_keep_sources_editable_without_a_vm() {
  Rig r;
  script::ScriptServices services;
  r.runtime.configureScripts(services);
  std::string saved, removed;
  script::ScriptSlots slots;
  slots.begin(r.runtime, services, false,
      [&](const std::string&, const std::string& source) { saved = source; },
      [&](const std::string& name) { removed = name; });
  TEST_ASSERT_NULL(slots.host());
  Command install(CommandType::ScriptSet);
  install.name = "broken";
  install.payload = "this source is deliberately invalid";
  TEST_ASSERT_EQUAL(static_cast<int>(DispatchResult::Ok), static_cast<int>(r.runtime.engine().execute(install)));
  TEST_ASSERT_EQUAL_STRING(install.payload.c_str(), saved.c_str());
  Command remove(CommandType::ScriptRemove);
  remove.name = "broken";
  TEST_ASSERT_EQUAL(static_cast<int>(DispatchResult::Ok), static_cast<int>(r.runtime.engine().execute(remove)));
  TEST_ASSERT_EQUAL_STRING("broken", removed.c_str());
}

struct RestoreFiles : script::IScriptFiles {
  std::vector<std::pair<std::string, std::string>> entries;
  void save(const std::string&, const std::string&) override {}
  void remove(const std::string&) override {}
  void storeChanged(const std::string&, const std::string&) override {}
  void loadAll(const LoadFn& visit) override {
    for (const auto& entry : entries) visit(entry.first, entry.second, "{}");
  }
  std::vector<std::string> names() const override { return {}; }
  bool readSource(const std::string&, std::string&) const override { return false; }
  bool readStore(const std::string&, std::string&) const override { return false; }
};

void test_boot_restores_modules_before_apps_that_import_them() {
  Rig r;
  script::ScriptServices services;
  r.runtime.configureScripts(services);
  services.monotonicMs = [] { return int64_t{0}; };
  script::ScriptSlots slots;
  slots.begin(r.runtime, services, true, {}, {});
  TEST_ASSERT_NOT_NULL(slots.host());
  RestoreFiles files;
  files.entries = {
      {"consumer", "import fmt\nclass App def draw() end end\nreturn App()"},
      {"fmt", "# @module\nvar m = module('fmt')\nm.tag = 'ok'\nreturn m"}};
  int refused = 0;
  script::restoreScripts(*slots.host(), files, 0,
      [&](const std::string&, const std::string&) { ++refused; });
  TEST_ASSERT_EQUAL_INT(0, refused);
  TEST_ASSERT_TRUE(slots.host()->errorOf("consumer").message.empty());
  TEST_ASSERT_NOT_NULL(r.runtime.apps().find("consumer"));
  TEST_ASSERT_NULL(r.runtime.apps().find("fmt"));
}

void test_screen_publisher_sends_the_current_canvas() {
  ScreenDisplay display;
  std::string topic, payload;
  display.setPublisher([&](const std::string& name, const std::string& body) { topic = name; payload = body; });
  display.sendScreen();
  TEST_ASSERT_TRUE(payload.empty());
  Canvas canvas(2, 1);
  canvas.setPixel(0, 0, 0x123456);
  display.setScreen(&canvas);
  display.sendScreen();
  TEST_ASSERT_EQUAL_STRING("state/screen", topic.c_str());
  TEST_ASSERT_EQUAL_STRING(buildScreenJson(canvas).c_str(), payload.c_str());
  const std::string before = payload;
  canvas.setPixel(1, 0, 0x654321);
  display.sendScreen();
  TEST_ASSERT_TRUE(payload != before);
}

void test_settings_save_coalesces_a_burst_without_postponing_retries() {
  SettingsSaver saver;
  TEST_ASSERT_FALSE(saver.due(1499));
  TEST_ASSERT_TRUE(saver.due(1500));
  saver.saved(1500);
  TEST_ASSERT_FALSE(saver.due(1600));
  TEST_ASSERT_TRUE(saver.due(3000));
  TEST_ASSERT_TRUE(saver.due(4500));
  SettingsSaver boot{-100000};
  TEST_ASSERT_TRUE(boot.due(0));
}

struct ProbeApp : IApp {
  std::string name = "Time";
  int64_t shownSinceMs = -2;
  const std::string& id() const override { return name; }
  void render(Canvas&, const RenderCtx& ctx) override { shownSinceMs = ctx.shownSinceMs; }
};

void test_queued_app_and_notification_affect_the_same_frame() {
  Rig r;
  auto& engine = r.runtime.engine();
  engine.state().settings().autoTransition = false;
  Command app(CommandType::SetPushedApp);
  app.name = "solid";
  app.payload = "{\"backgroundColor\":\"#123456\"}";
  TEST_ASSERT_TRUE(engine.submit(app));
  Command show(CommandType::SwitchApp);
  show.payload = "{\"name\":\"solid\",\"fast\":true}";
  TEST_ASSERT_TRUE(engine.submit(show));
  r.frame(0);
  TEST_ASSERT_EQUAL_STRING("solid", engine.currentAppId().c_str());
  TEST_ASSERT_EQUAL_HEX32(0x123456, r.runtime.canvas().getPixel(10, 3));

  Command notification(CommandType::Notify);
  notification.payload = "{\"backgroundColor\":\"#ABCDEF\",\"durationMs\":100}";
  TEST_ASSERT_TRUE(engine.submit(notification));
  r.frame(24);
  TEST_ASSERT_TRUE(engine.hasNotification());
  TEST_ASSERT_EQUAL_HEX32(0xABCDEF, r.runtime.canvas().getPixel(10, 3));
  engine.dismiss();
  r.frame(48);
  TEST_ASSERT_EQUAL_STRING("solid", engine.currentAppId().c_str());
  TEST_ASSERT_EQUAL_HEX32(0x123456, r.runtime.canvas().getPixel(10, 3));
}

void test_clock_frame_matches_the_registered_render_pipeline() {
  Rig r;
  r.frame(0);
  CoreEngine engine(r.audio, r.display, r.system);
  AppRegistry apps;
  TimeApp time;
  apps.add(&time);
  EffectRegistry effects, overlays;
  RenderPipelineDeps deps;
  deps.engine = &engine;
  deps.apps = &apps;
  deps.effects = &effects;
  deps.overlays = &overlays;
  deps.fonts = &awtrixFontCatalog();
  deps.audio = &r.audio;
  deps.clock = &r.clock;
  RenderPipeline pipeline(32, 8, deps);
  Canvas oldFrame(32, 8);
  engine.tick(0);
  pipeline.renderFrame(oldFrame, 0);
  TEST_ASSERT_EQUAL_UINT32_ARRAY(oldFrame.data(), r.runtime.canvas().data(), oldFrame.size());
  TEST_ASSERT_TRUE(test::countPixels(oldFrame, test::lit) > 0);
}

void test_registered_builtins_and_effects_are_available() {
  Rig r;
  TEST_ASSERT_NOT_NULL(r.runtime.apps().find("Time"));
  TEST_ASSERT_NOT_NULL(r.runtime.apps().find("Date"));
  TEST_ASSERT_NOT_NULL(r.runtime.apps().find("Temperature"));
  TEST_ASSERT_NOT_NULL(r.runtime.apps().find("Humidity"));
  TEST_ASSERT_NOT_NULL(r.runtime.apps().find("Battery"));
  DispatchDetail detail;
  TEST_ASSERT_EQUAL(static_cast<int>(DispatchResult::Ok), static_cast<int>(r.runtime.engine()
      .setPushedApp("weather", "{\"effect\":\"Plasma\",\"overlay\":\"Rain\"}", detail)));
}

struct SolidApp : IApp {
  std::string name;
  explicit SolidApp(std::string id = "Status") : name(std::move(id)) {}
  const std::string& id() const override { return name; }
  void render(Canvas& canvas, const RenderCtx&) override {
    canvas.fillRect(0, 0, canvas.width(), canvas.height(), 0x0A0B0Cu);
  }
};

void test_an_added_app_is_listed_and_drawn() {
  SolidApp status;
  Rig r([&](AppRegistry& apps) { apps.add(&status); });
  auto& engine = r.runtime.engine();
  TEST_ASSERT_EQUAL_STRING("Status", engine.knownApps().back().c_str());
  engine.state().settings().autoTransition = false;
  Command show(CommandType::SwitchApp);
  show.payload = "{\"name\":\"Status\",\"fast\":true}";
  TEST_ASSERT_TRUE(engine.submit(show));
  r.frame(0);
  TEST_ASSERT_EQUAL_STRING("Status", engine.currentAppId().c_str());
  TEST_ASSERT_EQUAL_HEX32(0x0A0B0C, r.runtime.canvas().getPixel(10, 3));
}

void test_a_replaced_app_keeps_its_place_and_a_removed_one_leaves() {
  SolidApp clock("Time");
  Rig r([&](AppRegistry& apps) { apps.add(&clock); apps.remove("Battery"); });
  auto& engine = r.runtime.engine();
  TEST_ASSERT_EQUAL_PTR(&clock, r.runtime.apps().find("Time"));
  TEST_ASSERT_NULL(r.runtime.apps().find("Battery"));
  const std::vector<std::string> expected = {"Time", "Date", "Temperature", "Humidity"};
  const std::vector<std::string> known = engine.knownApps();
  TEST_ASSERT_EQUAL_UINT(expected.size(), known.size());
  for (std::size_t i = 0; i < expected.size(); ++i) TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), known[i].c_str());
  engine.state().settings().autoTransition = false;
  r.frame(0);
  TEST_ASSERT_EQUAL_STRING("Time", engine.currentAppId().c_str());
  TEST_ASSERT_EQUAL_HEX32(0x0A0B0C, r.runtime.canvas().getPixel(10, 3));
}

void test_wakeup_notification_preserves_the_users_power_setting() {
  Rig r;
  r.frame(0);
  auto& engine = r.runtime.engine();
  engine.state().runtime().matrixOff = true;
  r.frame(24);
  TEST_ASSERT_TRUE(r.runtime.powerBusy());
  r.frame(624);
  TEST_ASSERT_FALSE(r.runtime.powerBusy());
  TEST_ASSERT_EQUAL_HEX32(0, r.runtime.canvas().getPixel(10, 3));

  DispatchDetail detail;
  TEST_ASSERT_EQUAL(static_cast<int>(DispatchResult::Ok), static_cast<int>(engine.notify(
      "{\"backgroundColor\":\"#ABCDEF\",\"wakeup\":true,\"hold\":true}", 0, detail)));
  r.frame(648);
  TEST_ASSERT_TRUE(r.runtime.powerBusy());
  r.frame(1248);
  TEST_ASSERT_EQUAL_HEX32(0xABCDEF, r.runtime.canvas().getPixel(10, 3));
  TEST_ASSERT_TRUE(engine.state().runtime().matrixOff);
  engine.dismiss();
  r.frame(1272);
  TEST_ASSERT_TRUE(r.runtime.powerBusy());
  r.frame(1872);
  TEST_ASSERT_FALSE(r.runtime.powerBusy());
  TEST_ASSERT_EQUAL_HEX32(0, r.runtime.canvas().getPixel(10, 3));
}

void test_power_and_moodlight_take_precedence_over_platform_frames() {
  Rig r;
  int calls = 0;
  auto external = [](Canvas& canvas, int64_t, void* context) {
    ++*static_cast<int*>(context);
    canvas.clear(0x123456);
    return PlatformFrame::ExternalRate;
  };
  TEST_ASSERT_TRUE(r.runtime.render(0, external, &calls));
  TEST_ASSERT_EQUAL_INT(1, calls);
  TEST_ASSERT_EQUAL_HEX32(0x123456, r.runtime.canvas().getPixel(10, 3));

  RuntimeState& state = r.runtime.engine().state().runtime();
  state.moodlightMode = true;
  state.moodlightColor = 0xABCDEF;
  state.moodlightBrightness = 37;
  TEST_ASSERT_FALSE(r.runtime.render(24, external, &calls));
  TEST_ASSERT_EQUAL_INT(1, calls);
  TEST_ASSERT_EQUAL_UINT8(37, r.brightness);
  TEST_ASSERT_EQUAL_HEX32(0xABCDEF, r.runtime.canvas().getPixel(10, 3));

  state.matrixOff = true;
  r.runtime.render(48, external, &calls);
  r.runtime.render(648, external, &calls);
  TEST_ASSERT_EQUAL_INT(1, calls);
  TEST_ASSERT_EQUAL_HEX32(0, r.runtime.canvas().getPixel(10, 3));
}

void test_every_frame_applies_and_reports_the_brightness_policy() {
  Rig r;
  auto& engine = r.runtime.engine();
  engine.state().settings().brightness = 64;
  r.runtime.beginFrame(0);
  TEST_ASSERT_EQUAL_UINT8(64, r.brightness);
  TEST_ASSERT_EQUAL_UINT8(64, engine.state().runtime().brightnessActual);

  engine.state().settings().autoBrightness = true;
  engine.state().runtime().lightLevel = 100.0f;
  r.frame(25);
  TEST_ASSERT_EQUAL_UINT8(64, r.brightness);

  engine.setLightSensorAvailable(true);
  r.frame(50);
  TEST_ASSERT_EQUAL_UINT8(LightConfig{}.maxBrightness, r.brightness);
  TEST_ASSERT_EQUAL_UINT8(LightConfig{}.maxBrightness, engine.state().runtime().brightnessActual);
}

void test_script_loop_runs_before_drawing_and_shared_commands_are_deferred() {
  Rig r;
  auto& engine = r.runtime.engine();
  engine.state().settings().autoTransition = false;
  int64_t now = 1000;
  script::ScriptServices services;
  services.monotonicMs = [&] { return now; };
  r.runtime.configureScripts(services);
  TEST_ASSERT_EQUAL_INT64(now, services.monotonicMs());
  script::ScriptHost scripts(r.runtime.apps(), services,
      [&](const std::string& id) { engine.syncScriptApp(id); },
      [&](const std::string& id) { engine.removeScriptApp(id); });
  script::ScriptService scriptService(scripts, nullptr, nullptr);
  engine.setScriptService(&scriptService);
  TEST_ASSERT_TRUE(scripts.set("counter",
      "class Counter\n"
      "  var count\n"
      "  def init() self.count = 0 end\n"
      "  def loop() self.count += 1 end\n"
      "  def draw() pixel(width() - 1, height() - 1, self.count) end\n"
      "end\nreturn Counter()"));
  Command show(CommandType::SwitchApp);
  show.payload = "{\"name\":\"counter\",\"fast\":true}";
  engine.submit(show);
  r.runtime.tick(now, &scripts);
  r.runtime.render(now);
  TEST_ASSERT_EQUAL_HEX32(1, r.runtime.canvas().getPixel(31, 7));
  now = 1024;
  r.runtime.tick(now, &scripts);
  r.runtime.render(now);
  TEST_ASSERT_EQUAL_HEX32(1, r.runtime.canvas().getPixel(31, 7));
  now = 2000;
  r.runtime.tick(now, &scripts);
  r.runtime.render(now);
  TEST_ASSERT_EQUAL_HEX32(2, r.runtime.canvas().getPixel(31, 7));

  TEST_ASSERT_TRUE(services.application->setSettings("{\"brightness\":17}"));
  TEST_ASSERT_EQUAL_INT(120, engine.state().settings().brightness);
  TEST_ASSERT_TRUE(services.application->setDisplayPower(false));
  TEST_ASSERT_FALSE(engine.state().runtime().matrixOff);
  r.runtime.tick(2024, &scripts);
  TEST_ASSERT_EQUAL_INT(17, engine.state().settings().brightness);
  TEST_ASSERT_TRUE(engine.state().runtime().matrixOff);
  engine.setScriptService(nullptr);
}

void test_apps_learn_since_when_they_are_fully_on_screen() {
  ProbeApp probe;
  Rig r([&](AppRegistry& apps) { apps.add(&probe); });
  auto& engine = r.runtime.engine();
  engine.state().settings().autoTransition = false;
  engine.state().settings().transitionDurationMs = 100;
  r.frame(0);
  r.frame(50);
  TEST_ASSERT_EQUAL_STRING("Time", engine.currentAppId().c_str());
  TEST_ASSERT_EQUAL_INT64(0, probe.shownSinceMs);

  Command away(CommandType::SwitchApp);
  away.payload = "{\"name\":\"Date\",\"fast\":true}";
  TEST_ASSERT_TRUE(engine.submit(away));
  r.frame(100);
  Command back(CommandType::SwitchApp);
  back.payload = "{\"name\":\"Time\"}";
  TEST_ASSERT_TRUE(engine.submit(back));
  r.frame(150);
  TEST_ASSERT_EQUAL_INT64(-1, probe.shownSinceMs);
  r.frame(300);
  TEST_ASSERT_EQUAL_STRING("Time", engine.currentAppId().c_str());
  TEST_ASSERT_EQUAL_INT64(300, probe.shownSinceMs);
  r.frame(350);
  TEST_ASSERT_EQUAL_INT64(300, probe.shownSinceMs);

  RuntimeState& state = engine.state().runtime();
  state.moodlightMode = true;
  r.frame(400);
  state.moodlightMode = false;
  r.frame(500);
  TEST_ASSERT_EQUAL_INT64(500, probe.shownSinceMs);
}

// The TC002's speaker in miniature: it synthesizes, and its loop layer plays until stopped.
struct Speaker : sound::RoutedPcmSink {
  std::string loop;
  bool ready = true;
  void setVolumes(const sound::Volumes&) override {}
  bool playMp3(const std::string&, sound::Group) override { return true; }
  void stopOneShot() override {}
  bool oneShotPlaying() const override { return false; }
  bool mixes() const override { return true; }
  bool playLoop(const std::string& path) override {
    loop = path;
    return true;
  }
  void stopLoop() override { loop.clear(); }
  bool loopPlaying() const override { return !loop.empty(); }
  bool synthesizes() const override { return true; }
  bool checkSong(const std::string&, std::string&) override { return true; }
  bool playSong(const std::string& text, bool) override {
    if (!ready) return false;
    loop = text;
    return true;
  }
  DispatchResult playStream(const std::string&, const std::string&, DispatchDetail&) override {
    return DispatchResult::Ok;
  }
  void stopStream() override {}
  void tick(int64_t) override {}
};

const char* const kTune =
    "class Tune\n"
    "  def on_show() sound.play({'song':'c d e','loop':true}) end\n"
    "  def on_hide() sound.stop('loop') end\n"
    "  def draw() end\n"
    "end\nreturn Tune()";

struct ScriptRig {
  Rig r;
  Speaker speaker;
  int64_t now = 1000;
  script::ScriptServices services;
  std::unique_ptr<script::ScriptHost> scripts;
  std::unique_ptr<script::ScriptService> service;

  ScriptRig() {
    r.audio.setPcm(&speaker);
    auto& engine = r.runtime.engine();
    engine.state().settings().autoTransition = false;
    services.monotonicMs = [this] { return now; };
    r.runtime.configureScripts(services);
    scripts.reset(new script::ScriptHost(r.runtime.apps(), services,
        [&engine](const std::string& id) { engine.syncScriptApp(id); },
        [&engine](const std::string& id) { engine.removeScriptApp(id); }));
    service.reset(new script::ScriptService(*scripts, nullptr, nullptr));
    engine.setScriptService(service.get());
  }
  ~ScriptRig() { r.runtime.engine().setScriptService(nullptr); }

  void show(const char* app) {
    Command c(CommandType::SwitchApp);
    c.payload = std::string("{\"name\":\"") + app + "\",\"fast\":true}";
    r.runtime.engine().submit(c);
    frames(3);
  }
  void frames(int count) {
    for (int i = 0; i < count; ++i) {
      now += 25;
      r.runtime.tick(now, scripts.get());
      r.runtime.render(now);
    }
  }
};

void test_deleting_a_script_silences_the_song_it_started() {
  ScriptRig s;
  TEST_ASSERT_TRUE(s.scripts->set("tune", kTune));
  s.show("tune");
  TEST_ASSERT_EQUAL_STRING("c d e", s.speaker.loop.c_str());

  s.scripts->remove("tune");
  s.frames(2);
  TEST_ASSERT_EQUAL_STRING("", s.speaker.loop.c_str());
}

void test_a_replaced_script_starts_from_silence() {
  ScriptRig s;
  TEST_ASSERT_TRUE(s.scripts->set("tune", kTune));
  s.show("tune");
  TEST_ASSERT_TRUE(s.scripts->set("tune", "class Quiet def draw() end end\nreturn Quiet()"));
  s.frames(3);
  TEST_ASSERT_EQUAL_STRING("", s.speaker.loop.c_str());

  TEST_ASSERT_TRUE(s.scripts->set("tune",
      "class Tune2\n"
      "  def on_show() sound.play({'song':'f g a','loop':true}) end\n"
      "  def draw() end\n"
      "end\nreturn Tune2()"));
  s.frames(3);
  TEST_ASSERT_EQUAL_STRING("f g a", s.speaker.loop.c_str());
}

void test_a_script_that_breaks_silences_its_song() {
  ScriptRig s;
  TEST_ASSERT_TRUE(s.scripts->set("tune",
      "class Tune\n"
      "  def on_show() sound.play({'song':'c d e','loop':true}) end\n"
      "  def draw() raise 'value_error', 'boom' end\n"
      "end\nreturn Tune()"));
  s.show("tune");
  s.frames(2);
  TEST_ASSERT_FALSE(s.scripts->errorOf("tune").message.empty());
  TEST_ASSERT_EQUAL_STRING("", s.speaker.loop.c_str());
}

void test_a_script_leaves_a_song_it_did_not_start() {
  ScriptRig s;
  TEST_ASSERT_TRUE(s.scripts->set("tune", kTune));
  TEST_ASSERT_TRUE(s.scripts->set("quiet", "class Quiet def draw() end end\nreturn Quiet()"));
  s.show("tune");
  s.scripts->remove("quiet");
  s.frames(2);
  TEST_ASSERT_EQUAL_STRING("c d e", s.speaker.loop.c_str());

  Command song(CommandType::PlayAudio);
  song.source = Source::Internal;
  song.name = "other";
  song.arg = static_cast<int>(sound::PlayAs::Once);
  song.payload = "{\"song\":\"g a b\",\"loop\":true}";
  s.r.runtime.engine().submit(song);
  s.frames(1);
  s.scripts->remove("tune");
  s.frames(2);
  TEST_ASSERT_EQUAL_STRING("g a b", s.speaker.loop.c_str());
}

// A script's sound call runs at once: a mistake answers with its field, an output the clock lacks
// answers false, a speaker that is only taken for now does not, and a stop then a play keep their
// order.
void test_a_scripts_sound_call_answers_at_once() {
  ScriptRig s;
  std::string error;
  const auto call = [&](script::SoundAction action, const char* json) {
    error.clear();
    return s.services.application->sound(action, json, "tune", error);
  };
  TEST_ASSERT_EQUAL_INT(-1, call(script::SoundAction::Play, "{\"mp3\":\"x\"}"));
  TEST_ASSERT_EQUAL_STRING("mp3: unknown field", error.c_str());
  TEST_ASSERT_EQUAL_INT(0, call(script::SoundAction::Play, "{\"speech\":\"Hi\"}"));
  TEST_ASSERT_EQUAL_INT(1, call(script::SoundAction::Play, "\"missing\""));
  s.speaker.ready = false;
  TEST_ASSERT_EQUAL_INT(1, call(script::SoundAction::Play, "{\"song\":\"c\",\"loop\":true}"));
  s.speaker.ready = true;
  TEST_ASSERT_EQUAL_INT(1, call(script::SoundAction::Play, "{\"song\":\"c d e\",\"loop\":true}"));
  TEST_ASSERT_EQUAL_INT(1, call(script::SoundAction::Stop, ""));
  TEST_ASSERT_EQUAL_INT(1, call(script::SoundAction::Play, "{\"song\":\"f g a\",\"loop\":true}"));
  s.frames(2);
  TEST_ASSERT_EQUAL_STRING("f g a", s.speaker.loop.c_str());
}
}

void setUp() {}
void tearDown() {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_disabled_scripts_keep_sources_editable_without_a_vm);
  RUN_TEST(test_boot_restores_modules_before_apps_that_import_them);
  RUN_TEST(test_screen_publisher_sends_the_current_canvas);
  RUN_TEST(test_settings_save_coalesces_a_burst_without_postponing_retries);
  RUN_TEST(test_queued_app_and_notification_affect_the_same_frame);
  RUN_TEST(test_clock_frame_matches_the_registered_render_pipeline);
  RUN_TEST(test_registered_builtins_and_effects_are_available);
  RUN_TEST(test_an_added_app_is_listed_and_drawn);
  RUN_TEST(test_a_replaced_app_keeps_its_place_and_a_removed_one_leaves);
  RUN_TEST(test_wakeup_notification_preserves_the_users_power_setting);
  RUN_TEST(test_power_and_moodlight_take_precedence_over_platform_frames);
  RUN_TEST(test_every_frame_applies_and_reports_the_brightness_policy);
  RUN_TEST(test_script_loop_runs_before_drawing_and_shared_commands_are_deferred);
  RUN_TEST(test_apps_learn_since_when_they_are_fully_on_screen);
  RUN_TEST(test_deleting_a_script_silences_the_song_it_started);
  RUN_TEST(test_a_replaced_script_starts_from_silence);
  RUN_TEST(test_a_scripts_sound_call_answers_at_once);
  RUN_TEST(test_a_script_that_breaks_silences_its_song);
  RUN_TEST(test_a_script_leaves_a_song_it_did_not_start);
  return UNITY_END();
}
