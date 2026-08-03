#include <unity.h>

#include <string>

#include "core/Dispatcher.h"
#include "core/StateStore.h"
#include "core/effects/EffectRegistry.h"

using namespace awtrix;

namespace {

struct FakeApp : IAppService {
  std::string customName, customJson, deletedName, orderJson, switchName;
  DispatchResult setReturn = DispatchResult::Ok;
  bool orderReturn = true, switchReturn = true;
  int next = 0, prev = 0;
  DispatchResult setPushedApp(const std::string& n, const std::string& j, DispatchDetail&) override { customName = n; customJson = j; return setReturn; }
  void deletePushedApp(const std::string& n) override { deletedName = n; }
  bool setAppOrder(const std::string& j) override { orderJson = j; return orderReturn; }
  bool switchApp(const std::string& n) override { switchName = n; return switchReturn; }
  void nextApp() override { ++next; }
  void previousApp() override { ++prev; }
};
struct FakeNotify : INotifyService {
  std::string json; uint8_t source = 99; DispatchResult ret = DispatchResult::Ok; int dismissed = 0;
  DispatchResult notify(const std::string& j, uint8_t s, DispatchDetail&) override { json = j; source = s; return ret; }
  void dismiss() override { ++dismissed; }
  std::string dismissedName; bool namedFound = true;
  bool dismissNamed(const std::string& n) override { dismissedName = n; return namedFound; }
};
struct FakeSound : ISoundService {
  std::string sound, rtttl, r2; bool soundRet = true; bool rtttlOk = true; int stops = 0;
  bool playSound(const std::string& p) override { sound = p; return soundRet; }
  void playRtttl(const std::string& p) override { rtttl = p; }
  void r2d2(const std::string& p) override { r2 = p; }
  void stop() override { ++stops; }
  bool supportsRtttl() const override { return rtttlOk; }
};
struct FakeDisplay : IDisplayService {
  int screens = 0;
  void sendScreen() override { ++screens; }
};
struct FakeSystem : ISystemService {
  int reboots = 0, factoryResets = 0, resets = 0; uint64_t sleepMs = 0;
  void reboot() override { ++reboots; }
  void sleep(uint64_t ms) override { sleepMs = ms; }
  void factoryReset() override { ++factoryResets; }
  void resetSettings() override { ++resets; }
};
struct FakeScripts : IScriptService {
  std::string setName, setSource, removedName;
  DispatchResult setReturn = DispatchResult::Ok;
  DispatchResult setScript(const std::string& n, const std::string& src, DispatchDetail&) override {
    setName = n; setSource = src; return setReturn;
  }
  void removeScript(const std::string& n) override { removedName = n; }
};
struct FakeOverlay : IEffect {
  std::string name;
  explicit FakeOverlay(std::string n) : name(std::move(n)) {}
  const std::string& id() const override { return name; }
  void render(Canvas&, int64_t) override {}
};

struct FakeRadio : IRadioService {
  std::string url, label; int stops = 0; int volume = -1; bool playing = false;
  DispatchResult playReturn = DispatchResult::Ok;
  DispatchResult play(const std::string& u, const std::string& l, DispatchDetail&) override {
    url = u; label = l; playing = playReturn == DispatchResult::Ok; return playReturn;
  }
  void stop() override { ++stops; playing = false; }
  void setVolume(int percent) override { volume = percent; }
  bool isPlaying() const override { return playing; }
};
struct FakeStations : IRadioStations {
  std::string lastJson; DispatchResult setReturn = DispatchResult::Ok;
  DispatchResult setStations(const std::string& j, DispatchDetail&) override {
    lastJson = j; return setReturn;
  }
  std::string stationsJson() const override { return "{\"stations\":[]}"; }
  std::string stationUrl(const std::string& name) const override {
    return name == "SWR3" ? "http://swr3.example/live" : std::string();
  }
  std::string stationNameAt(int index) const override {
    return index == 0 ? "SWR3" : std::string();
  }
};

struct Harness {
  StateStore state;
  FakeApp app; FakeNotify notify; FakeSound sound; FakeDisplay display; FakeSystem system;
  FakeScripts scripts;
  FakeRadio radio;
  FakeStations stations;
  FakeOverlay ovRain{"rain"}, ovSnow{"snow"};
  EffectRegistry overlays;
  Dispatcher d;
  CommandContext ctx{state, app, notify, sound, display, system};
  Harness() {
    overlays.add(&ovRain);
    overlays.add(&ovSnow);
    ctx.overlays = &overlays;
    ctx.scripts = &scripts;
    ctx.radio = &radio;
    ctx.stations = &stations;
  }
  DispatchResult run(const Command& c) { return d.dispatch(c, ctx); }
};

int rc(DispatchResult r) { return static_cast<int>(r); }

}

void test_radio_play_by_station_name() {
  Harness h;
  Command c(CommandType::RadioPlay);
  c.payload = "{\"station\":\"SWR3\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("http://swr3.example/live", h.radio.url.c_str());
  TEST_ASSERT_EQUAL_STRING("SWR3", h.radio.label.c_str());
  TEST_ASSERT_TRUE(h.state.runtime().radioPlaying);
  TEST_ASSERT_EQUAL_STRING("SWR3", h.state.runtime().radioStation.c_str());
}

void test_radio_play_by_index_and_url() {
  Harness h;
  Command byIndex(CommandType::RadioPlay);
  byIndex.payload = "{\"index\":0}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(byIndex)));
  TEST_ASSERT_EQUAL_STRING("SWR3", h.radio.label.c_str());

  Command byUrl(CommandType::RadioPlay);
  byUrl.payload = "{\"url\":\"https://ad.hoc/stream\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(byUrl)));
  TEST_ASSERT_EQUAL_STRING("https://ad.hoc/stream", h.radio.url.c_str());
}

void test_radio_play_rejects_unknown_and_malformed() {
  Harness h;
  Command unknown(CommandType::RadioPlay);
  unknown.payload = "{\"station\":\"nope\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::NotFound), rc(h.run(unknown)));

  Command badIndex(CommandType::RadioPlay);
  badIndex.payload = "{\"index\":7}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::NotFound), rc(h.run(badIndex)));

  Command badScheme(CommandType::RadioPlay);
  badScheme.payload = "{\"url\":\"ftp://x/\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ValidationError), rc(h.run(badScheme)));

  Command empty(CommandType::RadioPlay);
  empty.payload = "{}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ValidationError), rc(h.run(empty)));
  TEST_ASSERT_FALSE(h.state.runtime().radioPlaying);
}

void test_radio_stop_clears_the_title() {
  Harness h;
  h.state.runtime().radioPlaying = true;
  h.state.runtime().radioTitle = "Something";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(Command(CommandType::RadioStop))));
  TEST_ASSERT_EQUAL_INT(1, h.radio.stops);
  TEST_ASSERT_FALSE(h.state.runtime().radioPlaying);
  TEST_ASSERT_EQUAL_STRING("", h.state.runtime().radioTitle.c_str());
}

void test_radio_without_hardware_fails_but_stations_still_work() {
  Harness h;
  h.ctx.radio = nullptr;
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Unavailable),
                        rc(h.run(Command(CommandType::RadioStop))));
  Command play(CommandType::RadioPlay);
  play.payload = "{\"station\":\"SWR3\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Unavailable), rc(h.run(play)));

  Command set(CommandType::SetRadioStations);
  set.payload = "{\"stations\":[]}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(set)));
  TEST_ASSERT_EQUAL_STRING("{\"stations\":[]}", h.stations.lastJson.c_str());
}

void setUp() {}
void tearDown() {}

static void test_notify_routes_payload_and_source() {
  Harness h;
  Command c(CommandType::Notify);
  c.payload = "{\"text\":\"hi\"}";
  c.source = Source::Http;
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("{\"text\":\"hi\"}", h.notify.json.c_str());
  TEST_ASSERT_EQUAL_UINT8(1, h.notify.source);
}

static void test_notify_parse_error_maps_to_ParseError() {
  Harness h;
  h.notify.ret = DispatchResult::ParseError;
  Command c(CommandType::Notify);
  c.payload = "garbage";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ParseError), rc(h.run(c)));
}

static void test_notify_capacity_maps_to_Capacity() {
  Harness h;
  h.notify.ret = DispatchResult::Capacity;
  Command c(CommandType::Notify);
  c.payload = "{\"text\":\"x\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Capacity), rc(h.run(c)));
}

static void test_delete_app_removes_either_kind() {
  Harness h;
  Command c(CommandType::DeleteApp);
  c.name = "weather";
  c.clear = true;
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("weather", h.app.deletedName.c_str());
  TEST_ASSERT_EQUAL_STRING("weather", h.scripts.removedName.c_str());
}

static void test_delete_app_without_scripting_still_succeeds() {
  Harness h;
  h.ctx.scripts = nullptr;
  Command c(CommandType::DeleteApp);
  c.name = "weather";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("weather", h.app.deletedName.c_str());
}

static void test_custom_clear_flag_deletes() {
  Harness h;
  Command c(CommandType::SetPushedApp);
  c.name = "weather";
  c.clear = true;
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("weather", h.app.deletedName.c_str());
  TEST_ASSERT_EQUAL_STRING("", h.app.customName.c_str());
}

static void test_custom_empty_body_and_braces_delete_the_app() {
  Harness h;
  Command c(CommandType::SetPushedApp);
  c.name = "weather";
  c.payload = "";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("weather", h.app.deletedName.c_str());
  h.app.deletedName.clear();
  Command c2(CommandType::SetPushedApp);
  c2.name = "clock";
  c2.payload = "{}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c2)));
  TEST_ASSERT_EQUAL_STRING("clock", h.app.deletedName.c_str());
}

static void test_custom_capacity_maps_to_Capacity() {
  Harness h;
  h.app.setReturn = DispatchResult::Capacity;
  Command c(CommandType::SetPushedApp);
  c.name = "weather";
  c.payload = "{\"text\":\"x\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Capacity), rc(h.run(c)));
}

static void test_custom_normal_routes() {
  Harness h;
  Command c(CommandType::SetPushedApp);
  c.name = "weather";
  c.payload = "{\"text\":\"x\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("weather", h.app.customName.c_str());
}

static void test_switch_not_found_is_NotFound() {
  Harness h;
  h.app.switchReturn = false;
  Command c(CommandType::SwitchApp);
  c.name = "nope";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::NotFound), rc(h.run(c)));
}

static void test_next_previous() {
  Harness h;
  h.run(Command(CommandType::NextApp));
  h.run(Command(CommandType::PreviousApp));
  TEST_ASSERT_EQUAL_INT(1, h.app.next);
  TEST_ASSERT_EQUAL_INT(1, h.app.prev);
}

static void test_settings_applies_to_state() {
  Harness h;
  Command c(CommandType::SetSettings);
  c.payload = "{\"brightness\":200,\"soundEnabled\":false}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_INT(200, h.state.settings().brightness);
  TEST_ASSERT_FALSE(h.state.settings().soundEnabled);
}

static void test_settings_bad_json_is_ParseError() {
  Harness h;
  Command c(CommandType::SetSettings);
  c.payload = "{not json";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ParseError), rc(h.run(c)));
}

static void test_settings_invalid_field_is_ValidationError_and_atomic() {
  Harness h;
  Command c(CommandType::SetSettings);
  c.payload = "{\"brightness\":200,\"nope\":1}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ValidationError), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("nope", h.ctx.detail.field.c_str());
  TEST_ASSERT_FALSE(h.ctx.detail.message.empty());
  TEST_ASSERT_EQUAL_INT(120, h.state.settings().brightness);
}

static void test_settings_range_error_reports_field() {
  Harness h;
  Command c(CommandType::SetSettings);
  c.payload = "{\"volume\":99}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ValidationError), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("volume", h.ctx.detail.field.c_str());
}

static void test_indicator_sets_runtime() {
  Harness h;
  Command c(CommandType::SetIndicator);
  c.arg = 2;
  c.payload = "{\"color\":\"#FF0000\",\"blinkMs\":500}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_TRUE(h.state.runtime().indicators[1].on);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, h.state.runtime().indicators[1].color);
  TEST_ASSERT_EQUAL_UINT16(500, h.state.runtime().indicators[1].blinkMs);
}

static void test_indicator_clear_turns_off() {
  Harness h;
  h.state.runtime().indicators[0].on = true;
  Command c(CommandType::SetIndicator);
  c.arg = 1;
  c.clear = true;
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_FALSE(h.state.runtime().indicators[0].on);
}

static void test_indicator_empty_body_and_braces_turn_it_off() {
  for (const char* body : {"", "{}"}) {
    Harness h;
    h.state.runtime().indicators[0].on = true;
    Command c(CommandType::SetIndicator);
    c.arg = 1;
    c.payload = body;
    TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
    TEST_ASSERT_FALSE(h.state.runtime().indicators[0].on);
  }
}

static void test_indicator_unparseable_color_is_rejected() {
  Harness h;
  h.state.runtime().indicators[0].on = false;
  h.state.runtime().indicators[0].color = 0x00FF00u;
  Command c(CommandType::SetIndicator);
  c.arg = 1;
  c.payload = "{\"color\":\"not-a-colour\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ValidationError), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("color", h.ctx.detail.field.c_str());
  TEST_ASSERT_FALSE(h.state.runtime().indicators[0].on);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, h.state.runtime().indicators[0].color);
}

static void test_indicator_zero_and_null_still_turn_off() {
  for (const char* body : {"{\"color\":0}", "{\"color\":null}"}) {
    Harness h;
    h.state.runtime().indicators[0].on = true;
    h.state.runtime().indicators[0].color = 0x00FF00u;
    Command c(CommandType::SetIndicator);
    c.arg = 1;
    c.payload = body;
    TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
    TEST_ASSERT_FALSE(h.state.runtime().indicators[0].on);
    TEST_ASSERT_EQUAL_HEX32(0x00FF00u, h.state.runtime().indicators[0].color);
  }
}

static void test_moodlight_unparseable_color_is_rejected() {
  Harness h;
  h.state.runtime().moodlightMode = false;
  Command c(CommandType::Moodlight);
  c.payload = "{\"color\":\"not-a-colour\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ValidationError), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("color", h.ctx.detail.field.c_str());
  TEST_ASSERT_FALSE(h.state.runtime().moodlightMode);
}

static void test_moodlight_sets_runtime() {
  Harness h;
  Command c(CommandType::Moodlight);
  c.payload = "{\"color\":\"#FF0000\",\"brightness\":200}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_TRUE(h.state.runtime().moodlightMode);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, h.state.runtime().moodlightColor);
  TEST_ASSERT_EQUAL_UINT8(200, h.state.runtime().moodlightBrightness);
  Command off(CommandType::Moodlight);
  off.clear = true;
  h.run(off);
  TEST_ASSERT_FALSE(h.state.runtime().moodlightMode);
}

static void test_moodlight_brightness_only_keeps_color() {
  Harness h;
  Command set(CommandType::Moodlight);
  set.payload = "{\"color\":\"#FF0000\",\"brightness\":200}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(set)));

  Command dim(CommandType::Moodlight);
  dim.payload = "{\"brightness\":30}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(dim)));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, h.state.runtime().moodlightColor);
  TEST_ASSERT_EQUAL_UINT8(30, h.state.runtime().moodlightBrightness);
}

static void test_moodlight_defaults_to_white_before_any_color() {
  Harness h;
  Command c(CommandType::Moodlight);
  c.payload = "{\"brightness\":90}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_TRUE(h.state.runtime().moodlightMode);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, h.state.runtime().moodlightColor);
}

static void test_moodlight_empty_body_and_braces_turn_it_off() {
  for (const char* body : {"", "{}"}) {
    Harness h;
    h.state.runtime().moodlightMode = true;
    Command c(CommandType::Moodlight);
    c.payload = body;
    TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
    TEST_ASSERT_FALSE(h.state.runtime().moodlightMode);
  }
}

static void test_dismiss_without_name_drops_the_current_one() {
  Harness h;
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(Command(CommandType::DismissNotify))));
  TEST_ASSERT_EQUAL_INT(1, h.notify.dismissed);
  TEST_ASSERT_EQUAL_STRING("", h.notify.dismissedName.c_str());
}

static void test_dismiss_with_name_targets_that_notification() {
  Harness h;
  Command c(CommandType::DismissNotify);
  c.name = "backup-job";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("backup-job", h.notify.dismissedName.c_str());
  TEST_ASSERT_EQUAL_INT(0, h.notify.dismissed);
}

static void test_dismiss_with_unknown_name_is_NotFound() {
  Harness h;
  h.notify.namedFound = false;
  Command c(CommandType::DismissNotify);
  c.name = "never-sent";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::NotFound), rc(h.run(c)));
}

static void test_display_power_sets_matrixOff() {
  Harness h;
  Command c(CommandType::SetDisplay);
  c.payload = "{\"power\":false}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_TRUE(h.state.runtime().matrixOff);
}

static void test_display_overlay_set_and_clear() {
  Harness h;
  Command c(CommandType::SetDisplay);
  c.payload = "{\"overlay\":\"Rain\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("rain", h.state.runtime().globalOverlay.c_str());
  Command clr(CommandType::SetDisplay);
  clr.payload = "{\"overlay\":null}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(clr)));
  TEST_ASSERT_EQUAL_STRING("", h.state.runtime().globalOverlay.c_str());
}

static void test_display_overlay_unknown_is_ValidationError() {
  Harness h;
  Command c(CommandType::SetDisplay);
  c.payload = "{\"overlay\":\"lava\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ValidationError), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("overlay", h.ctx.detail.field.c_str());
  TEST_ASSERT_EQUAL_STRING("", h.state.runtime().globalOverlay.c_str());
}

static void test_display_overlay_empty_string_clears() {
  Harness h;
  h.state.runtime().globalOverlay = "rain";
  Command c(CommandType::SetDisplay);
  c.payload = "{\"overlay\":\"\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("", h.state.runtime().globalOverlay.c_str());
}

static void test_display_overlay_unchecked_without_registry() {
  Harness h;
  h.ctx.overlays = nullptr;
  Command c(CommandType::SetDisplay);
  c.payload = "{\"overlay\":\"lava\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("lava", h.state.runtime().globalOverlay.c_str());
}

static void test_display_patch_is_atomic() {
  Harness h;
  Command c(CommandType::SetDisplay);
  c.payload = "{\"power\":false,\"overlay\":\"lava\"}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ValidationError), rc(h.run(c)));
  TEST_ASSERT_FALSE(h.state.runtime().matrixOff);
}

static void test_display_overlay_settings_are_stored_and_clamped() {
  Harness h;
  Command c(CommandType::SetDisplay);
  c.payload = "{\"overlay\":\"rain\",\"overlaySettings\":{\"speed\":0.25}}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_TRUE(h.state.runtime().globalOverlaySettings.hasSpeed);
  TEST_ASSERT_EQUAL_FLOAT(0.25f, h.state.runtime().globalOverlaySettings.speed);

  Command over(CommandType::SetDisplay);
  over.payload = "{\"overlaySettings\":{\"speed\":99}}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(over)));
  TEST_ASSERT_EQUAL_FLOAT(10.0f, h.state.runtime().globalOverlaySettings.speed);
}

static void test_display_clearing_the_overlay_drops_its_settings() {
  Harness h;
  Command set(CommandType::SetDisplay);
  set.payload = "{\"overlay\":\"rain\",\"overlaySettings\":{\"speed\":0.25}}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(set)));
  Command clr(CommandType::SetDisplay);
  clr.payload = "{\"overlay\":null}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(clr)));
  TEST_ASSERT_FALSE(h.state.runtime().globalOverlaySettings.hasSpeed);
}

static void test_display_overlay_settings_must_be_an_object() {
  Harness h;
  Command c(CommandType::SetDisplay);
  c.payload = "{\"power\":false,\"overlaySettings\":7}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ValidationError), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("overlaySettings", h.ctx.detail.field.c_str());
  TEST_ASSERT_FALSE(h.state.runtime().matrixOff);
}

static void test_sleep_takes_durationMs() {
  Harness h;
  Command c(CommandType::Sleep);
  c.payload = "{\"durationMs\":42000}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_UINT(42000u, (unsigned)h.system.sleepMs);
  TEST_ASSERT_TRUE(h.state.runtime().matrixOff);
}

static void test_sleep_missing_duration_is_ValidationError() {
  Harness h;
  Command c(CommandType::Sleep);
  c.payload = "{}";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ValidationError), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("durationMs", h.ctx.detail.field.c_str());
}

static void test_sound_not_found_is_NotFound() {
  Harness h;
  h.sound.soundRet = false;
  Command c(CommandType::PlaySound);
  c.payload = "missing";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::NotFound), rc(h.run(c)));
}

static void test_rtttl_unsupported_backend_is_ValidationError() {
  Harness h;
  h.sound.rtttlOk = false;
  Command c(CommandType::PlayRtttl);
  c.payload = "x:d=4:c";
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::ValidationError), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("", h.sound.rtttl.c_str());
}

static void test_r2d2_muted_is_silent_ok() {
  Harness h;
  h.state.settings().soundEnabled = false;
  Command c(CommandType::R2D2);
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(c)));
  TEST_ASSERT_EQUAL_STRING("", h.sound.r2.c_str());
}

static void test_stop_sound_ignores_the_mute() {
  Harness h;
  h.state.settings().soundEnabled = false;
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Ok), rc(h.run(Command(CommandType::StopSound))));
  TEST_ASSERT_EQUAL_INT(1, h.sound.stops);
}

static void test_system_actions() {
  Harness h;
  h.run(Command(CommandType::Reboot));
  h.run(Command(CommandType::FactoryReset));
  h.run(Command(CommandType::ResetSettings));
  h.run(Command(CommandType::SendScreen));
  TEST_ASSERT_EQUAL_INT(1, h.system.reboots);
  TEST_ASSERT_EQUAL_INT(1, h.system.factoryResets);
  TEST_ASSERT_EQUAL_INT(1, h.system.resets);
  TEST_ASSERT_EQUAL_INT(1, h.display.screens);
}

static void test_unknown_command() {
  Harness h;
  TEST_ASSERT_EQUAL_INT(rc(DispatchResult::Unknown), rc(h.run(Command(CommandType::None))));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_radio_play_by_station_name);
  RUN_TEST(test_radio_play_by_index_and_url);
  RUN_TEST(test_radio_play_rejects_unknown_and_malformed);
  RUN_TEST(test_radio_stop_clears_the_title);
  RUN_TEST(test_radio_without_hardware_fails_but_stations_still_work);
  RUN_TEST(test_notify_routes_payload_and_source);
  RUN_TEST(test_notify_parse_error_maps_to_ParseError);
  RUN_TEST(test_notify_capacity_maps_to_Capacity);
  RUN_TEST(test_delete_app_removes_either_kind);
  RUN_TEST(test_delete_app_without_scripting_still_succeeds);
  RUN_TEST(test_custom_clear_flag_deletes);
  RUN_TEST(test_custom_empty_body_and_braces_delete_the_app);
  RUN_TEST(test_custom_capacity_maps_to_Capacity);
  RUN_TEST(test_custom_normal_routes);
  RUN_TEST(test_switch_not_found_is_NotFound);
  RUN_TEST(test_next_previous);
  RUN_TEST(test_settings_applies_to_state);
  RUN_TEST(test_settings_bad_json_is_ParseError);
  RUN_TEST(test_settings_invalid_field_is_ValidationError_and_atomic);
  RUN_TEST(test_settings_range_error_reports_field);
  RUN_TEST(test_indicator_sets_runtime);
  RUN_TEST(test_indicator_clear_turns_off);
  RUN_TEST(test_indicator_empty_body_and_braces_turn_it_off);
  RUN_TEST(test_indicator_unparseable_color_is_rejected);
  RUN_TEST(test_indicator_zero_and_null_still_turn_off);
  RUN_TEST(test_moodlight_unparseable_color_is_rejected);
  RUN_TEST(test_moodlight_sets_runtime);
  RUN_TEST(test_moodlight_brightness_only_keeps_color);
  RUN_TEST(test_moodlight_defaults_to_white_before_any_color);
  RUN_TEST(test_moodlight_empty_body_and_braces_turn_it_off);
  RUN_TEST(test_dismiss_without_name_drops_the_current_one);
  RUN_TEST(test_dismiss_with_name_targets_that_notification);
  RUN_TEST(test_dismiss_with_unknown_name_is_NotFound);
  RUN_TEST(test_display_power_sets_matrixOff);
  RUN_TEST(test_display_overlay_set_and_clear);
  RUN_TEST(test_display_overlay_unknown_is_ValidationError);
  RUN_TEST(test_display_overlay_empty_string_clears);
  RUN_TEST(test_display_overlay_unchecked_without_registry);
  RUN_TEST(test_display_patch_is_atomic);
  RUN_TEST(test_display_overlay_settings_are_stored_and_clamped);
  RUN_TEST(test_display_clearing_the_overlay_drops_its_settings);
  RUN_TEST(test_display_overlay_settings_must_be_an_object);
  RUN_TEST(test_sleep_takes_durationMs);
  RUN_TEST(test_sleep_missing_duration_is_ValidationError);
  RUN_TEST(test_sound_not_found_is_NotFound);
  RUN_TEST(test_stop_sound_ignores_the_mute);
  RUN_TEST(test_rtttl_unsupported_backend_is_ValidationError);
  RUN_TEST(test_r2d2_muted_is_silent_ok);
  RUN_TEST(test_system_actions);
  RUN_TEST(test_unknown_command);
  return UNITY_END();
}
