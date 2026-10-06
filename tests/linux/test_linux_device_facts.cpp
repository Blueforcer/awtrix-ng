#include "../../test/EngineFakes.h"
#include "../support.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

#include "core/CoreEngine.h"
#include "core/api/JsonWriter.h"
#include "core/script/ScriptHeap.h"
#include "platform/linux/host/HostScriptHeap.h"
#include "platform/linux/LinuxBoard.h"
#include "platform/linux/LinuxDeviceFacts.h"
#include "platform/linux/LinuxMemory.h"

using namespace awtrix;

namespace {
int interfaceMode = -1;
ifaddrs interfaceFixture{};
sockaddr_in addressFixture{};

int& failures = awtrix::test::failures();

using awtrix::test::check;

bool has(const std::string& json, const std::string& text) { return json.find(text) != std::string::npos; }

using FDisplay = awtrix::test::NullDisplay;

using FSystem = awtrix::test::NullSystem;

struct NetworkSource : DeviceFactsSource {
  void addFacts(DeviceFacts& facts) const override {
    facts.ipAddress = "192.0.2.7";
    facts.wifiRssi = -58;
  }
};

struct UpdateSource : DeviceFactsSource {
  void addFacts(DeviceFacts& facts) const override { facts.updateImage = "image.awup"; }
  void writeMembers(api::JsonWriter& json) const override {
    json.key("update").beginObject().member("state", "idle").endObject();
  }
};

}

extern "C" int __real_getifaddrs(ifaddrs**);
extern "C" void __real_freeifaddrs(ifaddrs*);
extern "C" int __wrap_getifaddrs(ifaddrs** out) {
  if (interfaceMode < 0) return __real_getifaddrs(out);
  if (interfaceMode == 5) return -1;
  interfaceFixture = {};
  addressFixture = {};
  addressFixture.sin_family = interfaceMode == 4 ? AF_INET6 : AF_INET;
  ::inet_pton(AF_INET, "192.0.2.44", &addressFixture.sin_addr);
  interfaceFixture.ifa_addr = reinterpret_cast<sockaddr*>(&addressFixture);
  interfaceFixture.ifa_flags = interfaceMode == 3 ? 0 : IFF_UP;
  if (interfaceMode == 2) interfaceFixture.ifa_flags |= IFF_LOOPBACK;
  *out = interfaceMode ? &interfaceFixture : nullptr;
  return 0;
}
extern "C" void __wrap_freeifaddrs(ifaddrs* interfaces) {
  if (interfaceMode < 0) __real_freeifaddrs(interfaces);
}

int main() {
  std::uint64_t bytes = 7;
  check(parseMemAvailable("MemTotal:       36004 kB\nMemFree:         9412 kB\n"
                          "MemAvailable:   15012 kB\nBuffers:            0 kB\n", bytes) &&
            bytes == 15012ULL * 1024,
        "MemAvailable is read in bytes");
  bytes = 7;
  check(!parseMemAvailable("MemTotal:       36004 kB\nMemFree:         9412 kB\n", bytes) && bytes == 7,
        "a kernel without MemAvailable leaves the value alone");
  check(!parseMemAvailable("MemAvailable:   lots kB\n", bytes) && bytes == 7, "a value that is no number");
  check(!parseMemAvailable("MemAvailable:   15012 MB\n", bytes) && bytes == 7, "a unit the kernel never uses");
  check(parseMemAvailable("MemAvailable: 1 kB", bytes) && bytes == 1024, "the last line without a newline");

  check(scriptHeapBudget(15ull << 20) == (15u << 20) / 4, "the script VM gets a quarter of what is available");
  check(scriptHeapBudget(64ull << 20) == kScriptHeapCapBytes, "at most the 4 MiB an S3 grants");
  check(scriptHeapBudget(0) == 0, "nothing available, no budget");

  std::size_t room = 123456;
  script::heap::configureHost("system", 3u << 20, [&room] { return room; });
  check(script::heap::info().name == std::string("system") && script::heap::info().budgetBytes == (3u << 20),
        "the platform names the pool and sets the budget");
  check(script::heap::growthBudget() == 123456, "growth room comes from the platform");
  room = 0;
  check(script::heap::growthBudget() == 0, "and follows it");
  LinuxMemoryGauge gauge;
  check(gauge.sample() && gauge.available() > 0 && gauge.lowest() == gauge.available(),
        "the gauge reads this host's /proc/meminfo");

  sound::AudioRouter audio;
  FDisplay display;
  FSystem system;
  CoreEngine engine(audio, display, system);
  LinuxBoard board(52, 16);
  LinuxDeviceFacts facts(engine, board, "0123456789abcdef", "tc002", "watchdog");
  std::string json = facts.json(false);
  check(has(json, "\"uid\":\"0123456789abcdef\",\"boardType\":\"tc002\""), "uid and board type: " + json);
  check(has(json, "\"resetReason\":\"watchdog\""), "the supervisor's start reason: " + json);
  timespec boot{};
  clock_gettime(CLOCK_BOOTTIME, &boot);
  const std::string uptime = "\"uptimeSeconds\":";
  const std::size_t at = json.find(uptime);
  const long reported = at == std::string::npos ? -1 : std::atol(json.c_str() + at + uptime.size());
  check(reported >= boot.tv_sec - 1 && reported <= boot.tv_sec + 1, "uptime counts from the boot: " + json);
  LinuxDeviceFacts headless(engine, board, "0123456789abcdef", "linux");
  check(has(headless.json(false), "\"resetReason\":\"unknown\""), "no start reason without a supervisor");
  check(!has(json, "\"freeHeapBytes\":0,"), "free RAM is MemAvailable, not zero: " + json);
  check(has(json, "\"minFreeHeapBytes\":"), "the low-water mark is reported");
  check(!has(json, "largestFreeBlockBytes"), "Linux cannot tell its largest free block");
  check(!has(json, "\"update\""), "no update member without a source");

  NetworkSource network;
  UpdateSource update;
  facts.addSource(network);
  facts.addSource(update);
  json = facts.json(true);
  check(has(json, "\"updateImage\":\"image.awup\",\"ipAddress\":\"192.0.2.7\""), "sources add facts: " + json);
  check(has(json, "\"wifiRssi\":-58,"), "the supervisor's signal replaces the default");
  check(has(json, "\"scriptingRunning\":true"), "scripting state");
  const std::string tail = "\"state\":\"off\"},\"update\":{\"state\":\"idle\"}}";
  check(json.size() > tail.size() && json.compare(json.size() - tail.size(), tail.size(), tail) == 0,
        "source members close the document: " + json);

  auto& wifi = engine.state().runtime().wifi;
  interfaceMode = 1;
  wifi.phase = net::LinkPhase::Connecting;
  headless.tick(0, false);
  check(wifi.phase == net::LinkPhase::Connecting, "interface observation leaves supervised state alone");
  headless.tick(1000, true);
  check(wifi.phase == net::LinkPhase::Connected && wifi.endpoint == "192.0.2.44",
        "unsupervised runtime reports an up IPv4 interface");
  int64_t now = 2000;
  for (int mode : {0, 2, 3, 4, 5}) {
    interfaceMode = mode;
    headless.tick(now, true);
    check(wifi.phase == net::LinkPhase::Offline && wifi.endpoint.empty(),
          "missing, loopback, down, IPv6-only or failed interfaces are offline");
    now += 1000;
  }
  interfaceMode = 1;
  headless.tick(now, true);
  check(wifi.phase == net::LinkPhase::Connected && wifi.error == net::LinkError::None,
        "restored interface clears the connection error");
  interfaceMode = -1;

  if (failures) return 1;
  std::puts("linux device facts: ok");
  return 0;
}
