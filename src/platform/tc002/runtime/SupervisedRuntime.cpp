#include "platform/tc002/runtime/SupervisedRuntime.h"
#include "core/sensing/BatteryModel.h"
#include "platform/tc002/contract/tc002_layout.h"
#include "core/launcher/MenuStyle.h"

#include <string.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/CoreEngine.h"
#include "core/api/ApiRouter.h"
#include "core/api/DiagnosticsApi.h"
#include "core/api/JsonWriter.h"
#include "core/api/StateJson.h"
#include "core/render/TextRenderer.h"
#include "persistence/DeviceConfig.h"
#include "platform/linux/LinuxBoard.h"
#include "platform/posix/Bytes.h"
#include "platform/tc002/runtime/SupervisorLink.h"
#include "system/Log.h"

namespace awtrix {
namespace {
void erase(std::string& secret) {
  if (!secret.empty()) ::explicit_bzero(&secret[0], secret.size());
  secret.clear();
}

// A blank password means "keep the saved one", as on ESP32. The supervisor saves only a key
// derived with the SSID, so it cannot carry that password over to another network.
const char* refusedCredentials(const DeviceConfig& next, bool allowOpen) {
  const tc002::WifiCredentialsProblem problem = tc002::checkWifiCredentials(next.wifiSsid, next.wifiPass);
  if (problem == tc002::WifiCredentialsProblem::Ssid) return "SSID must be 1 to 32 bytes";
  if (next.wifiPass.empty() && !allowOpen) return "a new network needs its password";
  if (problem == tc002::WifiCredentialsProblem::Password)
    return "password must be 8 to 63 characters or 64 hex digits";
  return nullptr;
}
}

net::WifiAssoc wifiAssociation(const tc002::NetworkStatus& network) {
  switch (network.link) {
    case tc002::WifiLink::Connected:
      return network.ipv4.empty() ? net::WifiAssoc::Joining : net::WifiAssoc::Connected;
    case tc002::WifiLink::Connecting: return net::WifiAssoc::Joining;
    case tc002::WifiLink::Failed: return net::WifiAssoc::AuthFailed;
    case tc002::WifiLink::AccessPoint:
    case tc002::WifiLink::Disconnected: return net::WifiAssoc::Disconnected;
    case tc002::WifiLink::Unconfigured: return net::WifiAssoc::Idle;
  }
  return net::WifiAssoc::Idle;
}

tc002::StaticAddress staticAddress(const DeviceConfig& config) {
  tc002::StaticAddress address;
  if (!config.netStatic || config.ip.empty()) return address;
  address = {true, config.ip, config.subnet, config.gateway, config.dns1, config.dns2};
  return address;
}

std::vector<std::string> supervisorConfigChanges(SupervisedSettings& applied, DeviceConfig& next, bool allowOpen) {
  std::vector<std::string> datagrams;
  if (next.wifiSsid != applied.wifiSsid || !next.wifiPass.empty()) {
    if (const char* reason = refusedCredentials(next, allowOpen)) {
      logf("wifi: \"%s\" not handed to the supervisor, keeping \"%s\" (%s)", next.wifiSsid.c_str(),
           applied.wifiSsid.c_str(), reason);
      next.wifiSsid = applied.wifiSsid;
    } else {
      tc002::WifiCredentials credentials{next.wifiSsid, next.wifiPass};
      datagrams.push_back(tc002::encodeWifi(credentials));
      erase(credentials.password);
      applied.wifiSsid = next.wifiSsid;
      logf("wifi: new network \"%s\" handed to the supervisor", next.wifiSsid.c_str());
    }
  }
  erase(next.wifiPass);
  if (next.ntpServer != applied.ntpServer) {
    datagrams.push_back(tc002::encodeNtp(next.ntpServer));
    applied.ntpServer = next.ntpServer;
  }
  if (next.hostname != applied.hostname) {
    datagrams.push_back(tc002::encodeHostname(next.hostname));
    applied.hostname = next.hostname;
  }
  const tc002::StaticAddress address = staticAddress(next);
  if (address != applied.address) {
    datagrams.push_back(tc002::encodeAddress(address));
    applied.address = address;
  }
  return datagrams;
}

SupervisedRuntime::SupervisedRuntime(SupervisorLink& link, CoreEngine& engine, LinuxBoard& board,
                                     DeviceConfig& config, SupervisedPageClock& clock)
    : link_(link), engine_(engine), board_(board), config_(config), clock_(clock) {}

void SupervisedRuntime::start() {
  if (!config_.wifiPass.empty()) {
    erase(config_.wifiPass);
    config_.save();
  }
  applied_ = {config_.wifiSsid, config_.ntpServer, config_.hostname, staticAddress(config_)};
  link_.send(tc002::encodeNtp(config_.ntpServer));
  link_.send(tc002::encodeHostname(config_.hostname));
  link_.send(tc002::encodeAddress(applied_.address));
  clock_.requireSynchronization();
}

void SupervisedRuntime::apply(const tc002::SupervisorMessage& message) {
  switch (message.type) {
    case tc002::MessageType::Power:
      applyPower(message.power);
      break;
    case tc002::MessageType::Network:
      applyNetwork(message.network);
      break;
    case tc002::MessageType::Bluetooth:
      if (onBluetooth_) onBluetooth_(message.bluetooth);
      break;
    case tc002::MessageType::WifiScanResult:
      if (scan_ == Scan::Running) {
        scanResult_ = message.networks;
        scan_ = Scan::Ready;
      }
      break;
    case tc002::MessageType::Time:
      if (message.time.synchronized && !clock_.synchronized()) {
        clock_.markSynchronized();
        logf("time: synchronized by the supervisor");
      }
      break;
    case tc002::MessageType::Log:
      logf("%s: %s", message.log.component.c_str(), message.log.text.c_str());
      break;
    default:
      break;
  }
}

void SupervisedRuntime::applyPower(const tc002::PowerStatus& power) {
  const bool supplyChanged = !powerKnown_ || power.usbPower != power_.usbPower;
  if (supplyChanged) logf("power: USB supply %s", power.usbPower ? "connected" : "absent");
  power_ = power;
  powerKnown_ = true;
  RuntimeState& rt = engine_.state().runtime();
  rt.externalPower = power.usbPower;
  if (supplyChanged) engine_.state().emit(StateEvent::PowerChanged);
  if (power.batteryPercent < 0) return;
  rt.batteryPercent = static_cast<uint8_t>(power.batteryPercent);
  rt.batteryVoltage = power.batteryMillivolts >= 0 ? static_cast<float>(power.batteryMillivolts) / 1000.0f : 0.0f;
  rt.lowBattery = isLowBattery(rt.batteryPercent, config_.lowBatteryThreshold);
  if (board_.hasBattery()) return;
  board_.setBatteryPresent(true);
  engine_.setBatteryAvailable(true);
  if (onBatteryAppeared_) onBatteryAppeared_();
}

void SupervisedRuntime::applyNetwork(const tc002::NetworkStatus& network) {
  const bool wasConnected = networkConnected();
  network_ = network;
  networkKnown_ = true;
  const std::string& ssid = network.ssid.empty() ? config_.wifiSsid : network.ssid;
  RuntimeState& rt = engine_.state().runtime();
  net::applyWifiAssoc(rt.wifi, wifiAssociation(network), network.link != tc002::WifiLink::Unconfigured, ssid,
                      network.ipv4);
  rt.wifiRssi = networkConnected() ? network.rssi : 0;
  if (networkConnected() && !wasConnected) {
    if (network.rssi < 0)
      logf("wifi: connected to \"%s\" (%d dBm) as %s", ssid.c_str(), network.rssi, network.ipv4.c_str());
    else
      logf("wifi: connected to \"%s\" as %s", ssid.c_str(), network.ipv4.c_str());
  } else if (!networkConnected() && wasConnected) {
    logf("wifi: connection lost (%s)", tc002::wifiLinkName(network.link));
  }
  if (networkConnected() && !network.ssid.empty() && network.ssid != config_.wifiSsid) {
    config_.wifiSsid = network.ssid;
    applied_.wifiSsid = network.ssid;
    config_.save();
  }
}

void SupervisedRuntime::configAccepted(DeviceConfig& next) {
  for (auto& datagram : supervisorConfigChanges(applied_, next, accessPoint())) link_.send(std::move(datagram));
}

void SupervisedRuntime::addFacts(DeviceFacts& facts) const {
  facts.ipAddress = networkConnected() ? network_.ipv4 : "0.0.0.0";
  if (!networkKnown_) return;
  if (!network_.hostname.empty()) facts.hostname = network_.hostname;
  facts.hasMacAddress = posix::parseMac(network_.mac, facts.macAddress.data());
  facts.wifiRssi = network_.link == tc002::WifiLink::Connected ? network_.rssi : 0;
}

void SupervisedRuntime::writeMembers(api::JsonWriter& json) const {
  if (powerKnown_) json.member("usbPower", power_.usbPower);
}

int SupervisedRuntime::wifiScan(int64_t nowMs, std::string& body) {
  if (accessPoint()) {
    body = api::errorJson("scanUnavailable", "Enter the Wi-Fi network name manually while using the setup hotspot");
    return 503;
  }
  if (scan_ == Scan::Running && nowMs - scanStartedMs_ > kScanTimeoutMs) scan_ = Scan::Idle;
  if (scan_ == Scan::Ready) {
    body.clear();
    api::JsonStream output([](void* context, const char* data, std::size_t size) {
      static_cast<std::string*>(context)->append(data, size);
    }, &body);
    api::WifiScanWriter writer(output);
    for (const auto& network : scanResult_) writer.network(network.ssid.c_str(), network.rssi, network.secure);
    writer.end();
    scanResult_.clear();
    scan_ = Scan::Idle;
    return 200;
  }
  if (scan_ == Scan::Idle && link_.send(tc002::encodeWifiScan())) {
    scan_ = Scan::Running;
    scanStartedMs_ = nowMs;
  }
  auto pending = api::wifiScanPending();
  body = std::move(pending.body);
  return pending.status;
}

bool SupervisedRuntime::networkConnected() const {
  return engine_.state().runtime().wifi.phase == net::LinkPhase::Connected;
}

void SupervisedRuntime::drawProvisioning(Canvas& canvas, const GfxFont& font, int64_t nowMs) const {
  canvas.clear(0);
  text::drawCentered(canvas, font, "AP MODE", 6, launcher::style::kCyan);
  const std::string line = network_.ssid + "   " TC002_AP_ADDRESS "   ";
  const int span = std::max(1, text::width(font, line));
  const int x = canvas.width() - static_cast<int>((nowMs / 65) % (canvas.width() + span));
  text::drawText(canvas, font, x, canvas.height() - 1, line, 0xFFFFFFu);
}

bool SupervisedRuntime::reboot() { return link_.send(tc002::encodeReboot()); }

bool SupervisedRuntime::factoryReset() { return link_.send(tc002::encodeFactoryReset()); }

bool SupervisedRuntime::requestBluetooth(bool on) {
  return link_.send(tc002::encodeBluetooth(tc002::BluetoothStatus{on, ""}));
}

}
