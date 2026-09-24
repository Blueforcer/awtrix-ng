#include <unity.h>
#include "system/GalacticUnicornControls.h"
#include "system/PeripheryService.cpp"
using namespace awtrix;
using namespace awtrix::galactic;
void setUp() {}
void tearDown() {}
struct Display : IDisplayService { void sendScreen() override {} };
struct System : ISystemService {
  void reboot() override {} void sleep(uint64_t) override {}
  void factoryReset() override {} void resetSettings() override {}
};
struct Board : IBoard, ISensorBus {
  ButtonState buttons{}; int raw = 0; uint8_t brightness = 0;
  const char* name() const override { return "test"; }
  int matrixWidth() const override { return 53; } int matrixHeight() const override { return 11; }
  void begin() override {} void show(const Canvas&) override {}
  void setBrightness(uint8_t b) override { brightness = b; }
  void setMatrixLayout(const MatrixLayout&) override {} void applyColorGrade(const render::GradeParams&) override {}
  bool hasBattery() const override { return false; } bool hasLightSensor() const override { return true; }
  int readBatteryMillivolts() override { return -1; } int readLdrRaw() override { return raw; }
  void pollButtons(ButtonState& out) override { out = buttons; }
  sound::IToneSink* toneSink() override { return nullptr; } sound::ITrackSink* trackSink() override { return nullptr; }
  ISensorBus& sensors() override { return *this; } bool hasSensor() const override { return false; }
  SensorReading read() override { return {}; } const char* sensorName() const override { return "none"; }
};
struct Fixture {
  sound::AudioRouter audio; Display display; System system; CoreEngine engine{audio, display, system};
  Board board; DeviceConfig cfg; PeripheryService periphery; GalacticUnicornControls controls;
  std::array<bool, 9> keys{}; int64_t now = 0; int settingsEvents = 0, powerEvents = 0, buttonEvents = 0;
  Fixture() {
    periphery.begin(engine, board, cfg);
    engine.state().subscribe([this](StateEvent e) {
      if (e == StateEvent::SettingsChanged) ++settingsEvents;
      if (e == StateEvent::PowerChanged) ++powerEvents;
      if (e == StateEvent::ButtonsChanged) ++buttonEvents;
    });
  }
  void sample() { controls.tick(engine, keys, now); periphery.tick(now); engine.tick(now); }
  void press(size_t key) {
    keys[key] = true; sample(); now += 35; sample();
    keys[key] = false; now += 1; sample(); now += 35; sample();
  }
};
void debounce_press_hold_bounce() {
  DebouncedButton b;
  TEST_ASSERT_FALSE(b.update(true, 10)); TEST_ASSERT_FALSE(b.update(false, 20));
  TEST_ASSERT_FALSE(b.update(true, 30)); TEST_ASSERT_FALSE(b.update(true, 64));
  TEST_ASSERT_TRUE(b.update(true, 65)); TEST_ASSERT_TRUE(b.pressed());
  TEST_ASSERT_FALSE(b.update(true, 10000));
  TEST_ASSERT_FALSE(b.update(false, 10001)); TEST_ASSERT_FALSE(b.update(true, 10010));
  TEST_ASSERT_FALSE(b.update(true, 10050)); TEST_ASSERT_TRUE(b.pressed());
  b.update(false, 10060); b.update(false, 10095); TEST_ASSERT_FALSE(b.pressed());
  b.update(true, 10100); TEST_ASSERT_TRUE(b.update(true, 10135));
}
void map_and_clamp() {
  const int pins[] = {0,1,3,6,27,7,8,21,26};
  const InputAction actions[] = {InputAction::Left,InputAction::Select,InputAction::Right,
    InputAction::None,InputAction::Power,InputAction::VolumeUp,InputAction::VolumeDown,
    InputAction::BrightnessUp,InputAction::BrightnessDown};
  for (size_t i=0; i<Inputs.size(); ++i) {
    TEST_ASSERT_EQUAL(pins[i], Inputs[i].pin); TEST_ASSERT_TRUE(actions[i] == Inputs[i].action);
  }
  TEST_ASSERT_EQUAL(0,stepClamped(2,-10,255)); TEST_ASSERT_EQUAL(255,stepClamped(250,10,255));
  TEST_ASSERT_EQUAL(0,stepClamped(0,-5,100)); TEST_ASSERT_EQUAL(100,stepClamped(100,5,100));
}
void extras_use_dispatcher_events() {
  Fixture f; auto& s = f.engine.state().settings();
  s.brightness = 250; s.autoBrightness = true; s.buzzerVolume = 98;
  f.press(7); TEST_ASSERT_EQUAL(255,s.brightness); TEST_ASSERT_FALSE(s.autoBrightness);
  f.press(8); TEST_ASSERT_EQUAL(245,s.brightness);
  f.press(5); TEST_ASSERT_EQUAL(100,s.buzzerVolume); f.press(6); TEST_ASSERT_EQUAL(95,s.buzzerVolume);
  s.brightness = 2; s.buzzerVolume = 2;
  f.press(8); f.press(6); TEST_ASSERT_EQUAL(0,s.brightness); TEST_ASSERT_EQUAL(0,s.buzzerVolume);
  TEST_ASSERT_EQUAL(6,f.settingsEvents); // Same persistence/MQTT subscription event as API.
  f.press(4); TEST_ASSERT_TRUE(f.engine.state().runtime().matrixOff);
  f.press(4); TEST_ASSERT_FALSE(f.engine.state().runtime().matrixOff); TEST_ASSERT_EQUAL(2,f.powerEvents);
  f.press(3); TEST_ASSERT_EQUAL(6,f.settingsEvents); TEST_ASSERT_EQUAL(2,f.powerEvents);
  f.keys[7] = true; f.sample(); f.now += 35; f.sample(); const int events = f.settingsEvents;
  f.now += 5000; f.sample(); TEST_ASSERT_EQUAL(events,f.settingsEvents); TEST_ASSERT_EQUAL(10,s.brightness);
}
void navigation_events_and_light() {
  Fixture f; auto& rt = f.engine.state().runtime();
  std::string callback; f.cfg.buttonCallback = "test";
  f.periphery.setButtonPost([&](const std::string&, const char* b, bool on, const std::string&) {
    callback = std::string(b) + (on ? ":on" : ":off");
  });
  f.engine.state().settings().blockNavigation = true; // Events still publish when navigation blocked.
  f.board.buttons.left = true; f.sample(); f.now = 34; f.sample(); TEST_ASSERT_EQUAL(0,f.buttonEvents);
  f.now = 35; f.sample(); TEST_ASSERT_TRUE(rt.buttons[0]); TEST_ASSERT_EQUAL_STRING("left:on",callback.c_str());
  f.now = 200; f.sample(); TEST_ASSERT_EQUAL(1,f.buttonEvents);
  f.board.buttons = {}; f.sample(); f.now += 35; f.sample(); TEST_ASSERT_FALSE(rt.buttons[0]);
  TEST_ASSERT_EQUAL_STRING("left:off",callback.c_str());
  f.board.buttons.select = true; f.sample(); f.now += 35; f.sample();
  TEST_ASSERT_TRUE(rt.buttons[1]); TEST_ASSERT_EQUAL_STRING("middle:on",callback.c_str());
  f.board.buttons.right = true; f.sample(); f.now += 35; f.sample();
  TEST_ASSERT_TRUE(rt.buttons[2]); TEST_ASSERT_EQUAL_STRING("right:on",callback.c_str());
  f.engine.state().settings().autoBrightness = true; f.cfg.brightnessSmoothing = 0;
  f.board.raw = 4095;
  for (int i=0; i<6; ++i) { f.now += 100; f.sample(); }
  TEST_ASSERT_EQUAL(4095,rt.ldrRaw); TEST_ASSERT_EQUAL(f.cfg.maxBrightness,f.board.brightness);
  f.board.raw = 0;
  for (int i=0; i<6; ++i) { f.now += 100; f.sample(); }
  TEST_ASSERT_EQUAL(0,rt.ldrRaw); TEST_ASSERT_EQUAL(f.cfg.minBrightness,f.board.brightness);
  f.engine.state().settings().autoBrightness = false; f.engine.state().settings().brightness = 123;
  f.now += 100; f.sample(); TEST_ASSERT_EQUAL(123,rt.brightnessActual);
}
void navigation_matches_commands_and_select_double_press() {
  for (bool swapped : {false,true}) for (int button : {0,2}) {
    Fixture f, api;
    f.cfg.swapButtons = swapped;
    f.sample(); api.sample();
    f.board.buttons = {button == 0, false, button == 2};
    f.sample(); f.now = 35; f.sample();
    const bool next = (button == 2) != swapped;
    api.engine.submit(Command(next ? CommandType::NextApp : CommandType::PreviousApp));
    api.now = 35; api.sample();
    f.now = api.now = 1000; f.sample(); api.sample();
    TEST_ASSERT_EQUAL_STRING(api.engine.currentAppId().c_str(),f.engine.currentAppId().c_str());
  }
  Fixture f;
  Command notification(CommandType::Notify); notification.payload = "{\"text\":\"test\",\"hold\":true}";
  TEST_ASSERT_TRUE(f.engine.execute(notification) == DispatchResult::Ok);
  TEST_ASSERT_TRUE(f.engine.hasNotification());
  f.board.buttons.select = true; f.sample(); f.now = 35; f.sample();
  TEST_ASSERT_FALSE(f.engine.hasNotification()); TEST_ASSERT_FALSE(f.engine.state().runtime().matrixOff);
  f.board.buttons.select = false; f.now = 36; f.sample(); f.now = 71; f.sample();
  f.board.buttons.select = true; f.now = 72; f.sample(); f.now = 107; f.sample();
  TEST_ASSERT_TRUE(f.engine.state().runtime().matrixOff);
  f.now = 1000; f.sample(); TEST_ASSERT_EQUAL(1,f.powerEvents);
}
int main() {
  UNITY_BEGIN(); RUN_TEST(debounce_press_hold_bounce); RUN_TEST(map_and_clamp);
  RUN_TEST(extras_use_dispatcher_events); RUN_TEST(navigation_events_and_light);
  RUN_TEST(navigation_matches_commands_and_select_double_press); return UNITY_END();
}
