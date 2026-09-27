#define AWTRIX_PLATFORM_RP2040
#include <unity.h>
#include "../../src/platform/rp2040/LittleFsPreferences.cpp"
#include "../../src/platform/rp2040/FilesystemRp2040.cpp"
#include "../../src/platform/rp2040/DisabledStores.cpp"
#include "../../src/persistence/DeviceConfig.cpp"
#include "../../src/persistence/NvsSettings.cpp"
#include "../../src/persistence/AppOrderStore.cpp"
#include "../../src/persistence/IconOriginsStore.cpp"
#include "../../src/persistence/DeviceConfigJson.cpp"
#include "../../src/persistence/SystemConfigApply.cpp"
#include "../../src/persistence/FsRestoreSink.cpp"
#include "../../src/persistence/LittleFsRestoreSink.cpp"
#include "core/StateStore.h"
using namespace awtrix;
namespace awtrix { void logf(const char*, ...) {} }
void setUp() { testFiles.clear(); testDirs.clear(); failWrite = failRename = false; }
void tearDown() {}
void legacy_panel_height_is_ignored_and_removed() {
  Preferences p;
  TEST_ASSERT_TRUE(p.begin("awtrix-cfg", false));
  p.putInt("ph", 16);
  p.end();
  DeviceConfig cfg; cfg.load();
  TEST_ASSERT_EQUAL(8, cfg.panelHeight);
  cfg.save();
  TEST_ASSERT_TRUE(p.begin("awtrix-cfg", true));
  TEST_ASSERT_FALSE(p.isKey("ph"));
  TEST_ASSERT_EQUAL(8, p.getInt("pheight", -1));
  p.end();
}
void panel_height_uses_fresh_key() {
  DeviceConfig cfg; cfg.panelHeight = 11; cfg.save();
  Preferences p;
  TEST_ASSERT_TRUE(p.begin("awtrix-cfg", true));
  TEST_ASSERT_EQUAL(11, p.getInt("pheight", -1));
  TEST_ASSERT_FALSE(p.isKey("ph"));
  p.end();
  // A stale legacy value must not override the fresh key either.
  TEST_ASSERT_TRUE(p.begin("awtrix-cfg", false));
  p.putInt("ph", 16);
  p.end();
  DeviceConfig loaded; loaded.load();
  TEST_ASSERT_EQUAL(11, loaded.panelHeight);
  loaded.save();
  TEST_ASSERT_TRUE(p.begin("awtrix-cfg", true));
  TEST_ASSERT_FALSE(p.isKey("ph"));
  TEST_ASSERT_EQUAL(11, p.getInt("pheight", -1));
  p.end();
}
void config_and_settings_reboot_roundtrip() {
  TEST_ASSERT_TRUE(fs::begin());
  TEST_ASSERT_TRUE(testDirs.count("/ICONS") && testDirs.count("/MELODIES") && testDirs.count("/PALETTES"));
  DeviceConfig before;
  before.wifiSsid = "test-ssid"; before.wifiPass = "test-pass";
  before.panelHeight = 11; before.mqttPort = 8883;
  before.tempOffset = -7.5f; before.wifiConnectTimeout = 32100;
  before.mqttEnabled = true; before.minBrightness = 22;
  before.save();
  DeviceConfig after; after.load();
  TEST_ASSERT_EQUAL_STRING("test-ssid", after.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("test-pass", after.wifiPass.c_str());
  TEST_ASSERT_EQUAL(11, after.panelHeight); TEST_ASSERT_EQUAL(8883, after.mqttPort);
  TEST_ASSERT_EQUAL_FLOAT(-7.5f, after.tempOffset);
  TEST_ASSERT_EQUAL(32100, after.wifiConnectTimeout);
  TEST_ASSERT_TRUE(after.mqttEnabled); TEST_ASSERT_EQUAL(22, after.minBrightness);
  Settings settings;
  settings.applyRead(api::JsonReader("{\"brightness\":42}"));
  TEST_ASSERT_EQUAL(42, settings.brightness);
  nvs::saveSettings(settings);
  Settings loaded; nvs::loadSettings(loaded);
  TEST_ASSERT_EQUAL(42, loaded.brightness);
  std::string a, b;
  api::JsonWriter wa(a), wb(b); settings.writeMembers(wa); loaded.writeMembers(wb);
  TEST_ASSERT_EQUAL_STRING(a.c_str(), b.c_str());
  testFiles["/NVS/awtrix-cfg.bin"][0] ^= 1;
  DeviceConfig defaults; defaults.load();
  TEST_ASSERT_TRUE(defaults.wifiSsid.empty());
}
void failed_writes_keep_last_config() {
  DeviceConfig cfg; cfg.wifiSsid = "old"; cfg.save();
  cfg.wifiSsid = "new";
  failWrite = true; cfg.save(); failWrite = false;
  DeviceConfig loaded; loaded.load(); TEST_ASSERT_EQUAL_STRING("old", loaded.wifiSsid.c_str());
  failRename = true; cfg.save(); failRename = false;
  loaded.load(); TEST_ASSERT_EQUAL_STRING("old", loaded.wifiSsid.c_str());
  TEST_ASSERT_FALSE(testFiles.count("/NVS/awtrix-cfg.bin.tmp"));
}
// A Galactic Unicorn refuses panel edits it cannot draw, and a restore keeps its own pins and panel
// while taking the rest of a backup made on a board wired differently.
void fixed_board_system_config() {
  const auto& soc = pins::rp2040Profile();
  DeviceConfig cfg;
  cfg.setPinSet(soc.defaults);
  cfg.panelWidth = 53; cfg.panelHeight = 11;
  int applied = 0; sysconfig::ApplyError err;
  auto put = [&](const char* json, sysconfig::Origin origin = sysconfig::Origin::Interactive) {
    err = {};
    return sysconfig::apply(cfg, api::JsonReader(json), applied, err, origin, soc);
  };
  TEST_ASSERT_FALSE(put(R"({"panelWidth":64})"));
  TEST_ASSERT_EQUAL(422, err.status); TEST_ASSERT_EQUAL_STRING("panelWidth", err.field.c_str());
  TEST_ASSERT_FALSE(put(R"({"panelHeight":14})"));
  TEST_ASSERT_EQUAL_STRING("panelHeight", err.field.c_str());
  TEST_ASSERT_FALSE(put(R"({"panelWiring":"columns"})"));
  TEST_ASSERT_EQUAL_STRING("panelWiring", err.field.c_str());
  TEST_ASSERT_EQUAL(53, cfg.panelWidth); TEST_ASSERT_EQUAL(11, cfg.panelHeight);
  TEST_ASSERT_TRUE(put(R"({"panelHeight":8,"mirror":true})"));
  TEST_ASSERT_EQUAL(8, cfg.panelHeight); TEST_ASSERT_TRUE(cfg.mirror);
  // An ESP32 backup: its own pins, width and wiring, and a height this panel cannot show.
  TEST_ASSERT_TRUE(put(R"({"hostname":"from-esp32","panelWidth":32,"panelHeight":16,)"
                       R"("panelWiring":"columns","pinMatrix":32,"pinBtnLeft":26,"pinLdr":45})",
                       sysconfig::Origin::Restore));
  TEST_ASSERT_EQUAL_STRING("from-esp32", cfg.hostname.c_str());
  TEST_ASSERT_EQUAL(53, cfg.panelWidth); TEST_ASSERT_EQUAL(8, cfg.panelHeight);
  TEST_ASSERT_TRUE(cfg.panelWiring == Wiring::Rows);
  TEST_ASSERT_EQUAL(-1, cfg.pinMatrix); TEST_ASSERT_EQUAL(0, cfg.pinBtnLeft);
  TEST_ASSERT_EQUAL(28, cfg.pinLdr);
  // Edited by hand, a pin past this chip's GPIOs is still out of range.
  TEST_ASSERT_FALSE(put(R"({"pinLdr":35})"));
  TEST_ASSERT_EQUAL_STRING("pinLdr", err.field.c_str());
  TEST_ASSERT_TRUE(put(R"({"panelHeight":11})", sysconfig::Origin::Restore));
  TEST_ASSERT_EQUAL(11, cfg.panelHeight);
  // Boards with configurable panels are unchanged.
  DeviceConfig esp;
  TEST_ASSERT_TRUE(sysconfig::apply(esp, api::JsonReader(R"({"panelWidth":64,"panelHeight":16})"),
                                    applied, err, sysconfig::Origin::Interactive,
                                    pins::esp32Profile()));
  TEST_ASSERT_EQUAL(64, esp.panelWidth); TEST_ASSERT_EQUAL(16, esp.panelHeight);
}
void fixed_board_adopted_at_boot() {
  const auto& soc = pins::rp2040Profile();
  // What an earlier build may have stored: another pin map (I2C on 4/5) and panel size.
  DeviceConfig cfg;
  cfg.setPinSet(pins::esp32Profile().defaults);
  cfg.pinI2cSda = 4; cfg.pinI2cScl = 5;
  cfg.panelWidth = 64; cfg.panels = 2; cfg.panelHeight = 16;
  int applied = 0; sysconfig::ApplyError err;
  TEST_ASSERT_FALSE(sysconfig::apply(cfg, api::JsonReader(R"({"debugMode":true})"), applied, err,
                                     sysconfig::Origin::Interactive, soc));
  sysconfig::adoptFixedBoard(cfg, soc);
  TEST_ASSERT_EQUAL(-1, cfg.pinI2cSda); TEST_ASSERT_EQUAL(-1, cfg.pinI2cScl);
  TEST_ASSERT_EQUAL(28, cfg.pinLdr); TEST_ASSERT_EQUAL(-1, cfg.pinBuzzer);
  TEST_ASSERT_EQUAL(53, cfg.panelWidth); TEST_ASSERT_EQUAL(1, cfg.panels);
  TEST_ASSERT_EQUAL(11, cfg.panelHeight);
  TEST_ASSERT_TRUE(sysconfig::apply(cfg, api::JsonReader(R"({"debugMode":true})"), applied, err,
                                    sysconfig::Origin::Interactive, soc));
  cfg.panelHeight = 8;
  sysconfig::adoptFixedBoard(cfg, soc);
  TEST_ASSERT_EQUAL(8, cfg.panelHeight);
  // A board whose pins and panel are the user's is left alone.
  DeviceConfig esp;
  esp.pinI2cSda = 17; esp.panelWidth = 64; esp.panelHeight = 16;
  sysconfig::adoptFixedBoard(esp, pins::esp32Profile());
  TEST_ASSERT_EQUAL(17, esp.pinI2cSda); TEST_ASSERT_EQUAL(64, esp.panelWidth);
  TEST_ASSERT_EQUAL(16, esp.panelHeight);
}
void restore_creates_nested_asset_directories() {
  DeviceConfig cfg; StateStore state; std::string err;
  backup::LittleFsRestoreSink sink(cfg, &state);
  testDirs.clear();
  TEST_ASSERT_TRUE(sink.beginFile("/ICONS/sub/deeper/smile.gif", err));
  TEST_ASSERT_TRUE(sink.writeFile(reinterpret_cast<const uint8_t*>("GIF8"), 4));
  TEST_ASSERT_TRUE(sink.endFile());
  TEST_ASSERT_EQUAL_STRING("GIF8", testFiles["/ICONS/sub/deeper/smile.gif"].c_str());
  TEST_ASSERT_TRUE(testDirs.count("/ICONS") && testDirs.count("/ICONS/sub") &&
                   testDirs.count("/ICONS/sub/deeper"));
  TEST_ASSERT_TRUE(sink.beginFile("/SCRIPTS/demo.be", err)); sink.abortFile();
  TEST_ASSERT_FALSE(sink.beginFile("/ICONS/sub/", err));
  TEST_ASSERT_FALSE(sink.beginFile("/ICONS//x.gif", err));
  TEST_ASSERT_FALSE(sink.beginFile("/apploop.json", err));
  TEST_ASSERT_FALSE(sink.beginFile("/ICONSX/x.gif", err));
}
void restore_and_disabled_stores() {
  DeviceConfig cfg; StateStore state; std::string err;
  backup::LittleFsRestoreSink sink(cfg, &state);
  TEST_ASSERT_TRUE(sink.applyWifi("restored", "test-pass", err));
  TEST_ASSERT_TRUE(cfg.wifiSsid.empty()); sink.commit();
  DeviceConfig rebooted; rebooted.load(); TEST_ASSERT_EQUAL_STRING("restored", rebooted.wifiSsid.c_str());
  testFiles["/ICONS/test.gif"] = "old";
  TEST_ASSERT_TRUE(sink.beginFile("/ICONS/test.gif", err));
  TEST_ASSERT_TRUE(sink.writeFile(reinterpret_cast<const uint8_t*>("new"), 3));
  sink.abortFile(); TEST_ASSERT_EQUAL_STRING("old", testFiles["/ICONS/test.gif"].c_str());
  TEST_ASSERT_TRUE(sink.beginFile("/ICONS/test.gif", err));
  TEST_ASSERT_TRUE(sink.writeFile(reinterpret_cast<const uint8_t*>("new"), 3));
  TEST_ASSERT_TRUE(sink.endFile()); TEST_ASSERT_EQUAL_STRING("new", testFiles["/ICONS/test.gif"].c_str());
  TEST_ASSERT_FALSE(sink.beginFile("/NVS/awtrix-cfg.bin", err));
  TEST_ASSERT_FALSE(sink.beginFile("/ICONS/../bad", err));
  // ScriptStore is the shared LittleFS one now that the Pico runs scripts; it needs directory
  // listing this fake filesystem does not model, so it is exercised on the device (smoke.sh).
  radiostore::save("[]"); TEST_ASSERT_FALSE(testFiles.count("/radio.json"));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(legacy_panel_height_is_ignored_and_removed);
  RUN_TEST(panel_height_uses_fresh_key);
  RUN_TEST(config_and_settings_reboot_roundtrip);
  RUN_TEST(failed_writes_keep_last_config);
  RUN_TEST(restore_and_disabled_stores);
  RUN_TEST(restore_creates_nested_asset_directories);
  RUN_TEST(fixed_board_system_config);
  RUN_TEST(fixed_board_adopted_at_boot);
  return UNITY_END();
}
