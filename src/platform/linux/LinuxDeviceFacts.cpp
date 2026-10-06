#include "platform/linux/LinuxDeviceFacts.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/utsname.h>

#include <ctime>

#include "core/CoreEngine.h"
#include "core/api/JsonWriter.h"
#include "core/net/WifiLink.h"
#include "hal/IBoard.h"
#include "system/MonotonicClock.h"

namespace awtrix {
namespace {

std::string firstIpv4Address() {
  std::string address = "127.0.0.1";
  ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) != 0) return address;
  for (auto* it = interfaces; it; it = it->ifa_next) {
    if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || !(it->ifa_flags & IFF_UP) ||
        (it->ifa_flags & IFF_LOOPBACK))
      continue;
    char text[INET_ADDRSTRLEN]{};
    const auto* ipv4 = reinterpret_cast<sockaddr_in*>(it->ifa_addr);
    if (inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text))) address = text;
    break;
  }
  freeifaddrs(interfaces);
  return address;
}

// Seconds since the system booted, as uptimeSeconds counts them on the ESP32.
long secondsSinceBoot() {
  timespec now{};
  if (clock_gettime(CLOCK_BOOTTIME, &now) == 0) return static_cast<long>(now.tv_sec);
  return static_cast<long>(monotonicMs() / 1000);
}

}

LinuxDeviceFacts::LinuxDeviceFacts(CoreEngine& engine, IBoard& board, std::string uid,
                                   std::string boardType, std::string resetReason)
    : engine_(engine),
      board_(board),
      uid_(std::move(uid)),
      boardType_(std::move(boardType)),
      resetReason_(std::move(resetReason)) {}

std::string LinuxDeviceFacts::nodename() const {
  utsname info{};
  return uname(&info) == 0 ? info.nodename : "";
}

void LinuxDeviceFacts::tick(int64_t nowMs, bool observeNetwork) {
  memory_.tick(nowMs);
  if (nowMs < nextNameMs_) return;
  nextNameMs_ = nowMs + kNameRefreshMs;
  if (observeNetwork) {
    const std::string address = firstIpv4Address();
    const bool connected = address != "127.0.0.1";
    net::applyWifiAssoc(engine_.state().runtime().wifi,
        connected ? net::WifiAssoc::Connected : net::WifiAssoc::Disconnected,
        true, {}, connected ? address : std::string());
  }
  DeviceFacts facts;
  facts.hostname = nodename();
  for (const DeviceFactsSource* source : sources_) source->addFacts(facts);
  hostname_ = facts.hostname;
}

std::string LinuxDeviceFacts::ipAddress() const {
  DeviceFacts facts;
  facts.ipAddress = firstIpv4Address();
  for (const DeviceFactsSource* source : sources_) source->addFacts(facts);
  return facts.ipAddress;
}

std::string LinuxDeviceFacts::json(bool scriptingRunning) {
  DeviceFacts facts;
  facts.boardType = boardType_;
  utsname info{};
  if (uname(&info) == 0) {
    facts.soc = info.machine;
    facts.hostname = info.nodename;
  }
  facts.ipAddress = firstIpv4Address();
  facts.uptimeSeconds = secondsSinceBoot();
  if (memory_.sample()) {
    facts.freeHeapBytes = memory_.available();
    facts.minFreeHeapBytes = memory_.lowest();
  }
  facts.resetReason = resetReason_;
  facts.hasBattery = board_.hasBattery();
  facts.hasLightSensor = board_.hasLightSensor();
  facts.scriptingRunning = scriptingRunning;
  for (const DeviceFactsSource* source : sources_) source->addFacts(facts);
  return buildDeviceJson(engine_, uid_, facts, [this](api::JsonWriter& json) {
    for (const DeviceFactsSource* source : sources_) source->writeMembers(json);
  });
}

}
