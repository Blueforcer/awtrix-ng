#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/mqtt/Entity.h"
#include "core/net/WifiLink.h"
#include "core/SystemPageClock.h"
#include "platform/linux/LinuxDeviceFacts.h"
#include "platform/tc002/contract/SupervisorProtocol.h"

namespace awtrix {
class CoreEngine;
class LinuxBoard;
class SupervisorLink;
struct DeviceConfig;

net::WifiAssoc wifiAssociation(const tc002::NetworkStatus& network);

inline constexpr ha::Entity kChargingEntity{
    "chg",
    R"J("p":"binary_sensor","name":"Charging","dev_cla":"battery_charging","stat_t":"~/state/device","val_tpl":"{{ 'ON' if value_json.usbPower else 'OFF' }}")J"};

// Under a supervisor the wall clock only counts as set once the supervisor reports it synchronized.
class SupervisedPageClock : public IPageClock {
 public:
  void requireSynchronization() { gated_ = true; }
  void markSynchronized() { synchronized_ = true; }
  bool synchronized() const { return !gated_ || synchronized_; }
  void fill(RenderCtx& ctx, int64_t nowMs) override {
    clock_.fill(ctx, nowMs);
    if (!synchronized()) ctx.epochMs = -1;
  }

 private:
  SystemPageClock clock_;
  bool gated_ = false;
  bool synchronized_ = false;
};

struct SupervisedSettings {
  std::string wifiSsid, ntpServer, hostname;
  tc002::StaticAddress address;
};

// A fixed address needs netStatic and an ip, as on ESP32.
tc002::StaticAddress staticAddress(const DeviceConfig& config);

// Returns the datagrams an accepted configuration requires and removes the Wi-Fi password from
// it: the supervisor keeps the credentials, the runtime only keeps the SSID for display. A Wi-Fi
// change the supervisor cannot take is dropped and the SSID reverts to the applied one.
std::vector<std::string> supervisorConfigChanges(SupervisedSettings& applied, DeviceConfig& next, bool allowOpen = false);

// As a device facts source it reports the network the supervisor manages.
class SupervisedRuntime : public DeviceFactsSource {
 public:
  SupervisedRuntime(SupervisorLink& link, CoreEngine& engine, LinuxBoard& board, DeviceConfig& config,
                    SupervisedPageClock& clock);
  void start();
  void apply(const tc002::SupervisorMessage& message);
  void configAccepted(DeviceConfig& next);
  void addFacts(DeviceFacts& facts) const override;
  void writeMembers(api::JsonWriter& json) const override;
  // GET /api/v1/system/wifi-scan as on ESP32: 202 while a scan runs, then the result once.
  int wifiScan(int64_t nowMs, std::string& body);
  bool accessPoint() const { return network_.link == tc002::WifiLink::AccessPoint; }
  bool networkConnected() const;
  void drawProvisioning(Canvas& canvas, const GfxFont& font, int64_t nowMs) const;
  bool reboot();
  bool factoryReset();
  bool requestBluetooth(bool on);
  void setOnBatteryAppeared(std::function<void()> callback) { onBatteryAppeared_ = std::move(callback); }
  void setOnBluetooth(std::function<void(const tc002::BluetoothStatus&)> callback) {
    onBluetooth_ = std::move(callback);
  }

 private:
  void applyPower(const tc002::PowerStatus& power);
  void applyNetwork(const tc002::NetworkStatus& network);

  enum class Scan : uint8_t { Idle, Running, Ready };
  static constexpr int64_t kScanTimeoutMs = 15000;

  SupervisorLink& link_;
  CoreEngine& engine_;
  LinuxBoard& board_;
  DeviceConfig& config_;
  SupervisedPageClock& clock_;
  SupervisedSettings applied_;
  tc002::PowerStatus power_;
  tc002::NetworkStatus network_;
  bool powerKnown_ = false;
  bool networkKnown_ = false;
  Scan scan_ = Scan::Idle;
  int64_t scanStartedMs_ = 0;
  std::vector<tc002::WifiNetwork> scanResult_;
  std::function<void()> onBatteryAppeared_;
  std::function<void(const tc002::BluetoothStatus&)> onBluetooth_;
};

}
