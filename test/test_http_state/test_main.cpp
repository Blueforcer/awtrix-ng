#include <unity.h>
#include "core/CoreEngine.h"
#include "core/api/StateJson.h"
#include "core/api/JsonReader.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptInfo.h"
#include "platform/rp2040/WifiCompat.h"
#include "transport/http/BodyArena.h"

using namespace awtrix;
void setUp() {}
void tearDown() {}
struct Display : IDisplayService { void sendScreen() override {} };
struct System : ISystemService {
  void reboot() override {} void sleep(uint64_t) override {}
  void factoryReset() override {} void resetSettings() override {}
};
void state_without_script_host() {
  sound::AudioRouter audio;
  Display display; System system;
  CoreEngine engine(audio, display, system);
  TEST_ASSERT_TRUE(script::scriptInfo(nullptr).empty());
  const auto apps = buildAppsJson(engine);
  TEST_ASSERT_TRUE(api::isWellFormed(apps));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, apps.find("\"name\":\"Time\""));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, apps.find("\"origin\":\"builtin\""));
  TEST_ASSERT_EQUAL(std::string::npos, apps.find("\"origin\":\"script\""));
  DeviceFacts facts;
  facts.soc = "rp2040"; facts.freeHeapBytes = 12345; facts.resetReason = "software";
  const auto device = buildDeviceJson(engine, "test-id", facts);
  TEST_ASSERT_TRUE(api::isWellFormed(device));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, device.find("\"scriptingRunning\":false"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, device.find("\"freeHeapBytes\":12345"));
  TEST_ASSERT_EQUAL(std::string::npos, device.find("psramTotalBytes"));
  Canvas screen(53, 11);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, buildScreenJson(screen).find("\"height\":11"));
}
void scan_starts_polls_and_restarts_after_empty_result() {
  struct Wifi {
    int starts = 0, result = 0;
    void scanNetworks(bool async) { TEST_ASSERT_TRUE(async); ++starts; }
    int scanComplete() { return result; }
  } wifi;
  platform::pico::WifiScan scan;
  TEST_ASSERT_EQUAL(-1, scan.poll(wifi));
  TEST_ASSERT_EQUAL(1, wifi.starts);
  wifi.result = -1;
  TEST_ASSERT_EQUAL(-1, scan.poll(wifi));
  TEST_ASSERT_EQUAL(1, wifi.starts);
  wifi.result = 0;
  TEST_ASSERT_EQUAL(0, scan.poll(wifi));
  scan.consumed();
  TEST_ASSERT_EQUAL(-1, scan.poll(wifi));
  TEST_ASSERT_EQUAL(2, wifi.starts);
  wifi.result = 3;
  TEST_ASSERT_EQUAL(3, scan.poll(wifi));
}
void raw_body_limit_is_eight_kib_and_recovers_after_overflow() {
  BodyArena arena;
  TEST_ASSERT_TRUE(arena.init(8192));
  std::string body(8192, ' ');
  arena.open(8192); arena.append(body.data(), body.size()); arena.finish();
  TEST_ASSERT_EQUAL(8192, arena.view().size());
  arena.open(8192); arena.append(body.data(), body.size()); arena.append("x", 1); arena.finish();
  TEST_ASSERT_TRUE(arena.state() == BodyArena::State::Overflow);
  TEST_ASSERT_TRUE(arena.view().empty());
  arena.reset(); arena.open(8192); arena.append("{}", 2); arena.finish();
  TEST_ASSERT_EQUAL(2, arena.view().size());
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(state_without_script_host);
  RUN_TEST(scan_starts_polls_and_restarts_after_empty_result);
  RUN_TEST(raw_body_limit_is_eight_kib_and_recovers_after_overflow);
  return UNITY_END();
}
