#include "platform/linux/LinuxDeviceFacts.h"

#include <ctime>

#include "core/CoreEngine.h"
#include "core/api/JsonWriter.h"
#include "core/net/WifiLink.h"
#include "hal/IBoard.h"
#include "platform/host/Process.h"
#include "platform/host/System.h"

namespace awtrix {

LinuxDeviceFacts::LinuxDeviceFacts(CoreEngine& engine, IBoard& board, std::string uid,
                                   std::string boardType, std::string resetReason)
    : engine_(engine),
      board_(board),
      uid_(std::move(uid)),
      boardType_(std::move(boardType)),
      resetReason_(std::move(resetReason)) {}

std::string LinuxDeviceFacts::nodename() const { return os::hostName(); }

void LinuxDeviceFacts::tick(int64_t nowMs, bool observeNetwork) {
  memory_.tick(nowMs);
  if (nowMs < nextNameMs_) return;
  nextNameMs_ = nowMs + kNameRefreshMs;
  if (observeNetwork) {
    const std::string address = os::firstIpv4Address();
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
  facts.ipAddress = os::firstIpv4Address();
  for (const DeviceFactsSource* source : sources_) source->addFacts(facts);
  return facts.ipAddress;
}

std::string LinuxDeviceFacts::json(bool scriptingRunning) {
  DeviceFacts facts;
  facts.boardType = boardType_;
  facts.soc = os::machineName();
  facts.hostname = os::hostName();
  facts.ipAddress = os::firstIpv4Address();
  facts.uptimeSeconds = os::secondsSinceBoot();
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
