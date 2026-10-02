#include <unity.h>
#include "core/CoreEngine.h"
#include "core/api/StateJson.h"
#include "core/api/JsonReader.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptInfo.h"
#include "platform/rp2040/WifiCompat.h"
#include "transport/http/BodyArena.h"
#include "transport/http/RequestHead.h"

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
void request_head_is_found_however_it_is_split() {
  const std::string req = "PUT /api/v1/system HTTP/1.1\r\nHost: x\r\nContent-Length: 12\r\n\r\n{\"a\":true}";
  const std::size_t want = req.find("\r\n\r\n") + 4;
  // Cut into two reads at every offset: the blank line is found whichever read it straddles.
  for (std::size_t cut = 0; cut <= req.size(); ++cut) {
    const std::size_t first = requesthead::end(req.data(), cut);
    TEST_ASSERT_EQUAL(cut >= want ? want : 0, first);
    if (!first) TEST_ASSERT_EQUAL(want, requesthead::end(req.data(), req.size(), cut));
  }
  const std::string open = "GET / HTTP/1.1\r\nHost: x\r\n";
  TEST_ASSERT_EQUAL(0, requesthead::end(open.data(), open.size()));
  TEST_ASSERT_EQUAL(12, requesthead::contentLength(req.data(), want));
  const std::string mixed = "PUT / HTTP/1.1\r\ncontent-LENGTH: 3000\r\n\r\n";
  TEST_ASSERT_EQUAL(3000, requesthead::contentLength(mixed.data(), mixed.size()));
  const std::string inside = "GET / HTTP/1.1\r\nX-Content-Length: 5\r\n\r\n";
  TEST_ASSERT_EQUAL(0, requesthead::contentLength(inside.data(), inside.size()));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(state_without_script_host);
  RUN_TEST(scan_starts_polls_and_restarts_after_empty_result);
  RUN_TEST(raw_body_limit_is_eight_kib_and_recovers_after_overflow);
  RUN_TEST(request_head_is_found_however_it_is_split);
  return UNITY_END();
}
