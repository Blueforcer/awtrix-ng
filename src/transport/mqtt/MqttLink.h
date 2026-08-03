#pragma once

#include <PubSubClient.h>
#include <WiFiClient.h>

#include <cstdint>
#include <functional>
#include <string>

#include "core/net/ConnectionBackoff.h"
#include "core/net/LinkStatus.h"
#include "persistence/DeviceConfig.h"
#include "transport/net/HostResolver.h"

namespace awtrix {

class MqttLink {
 public:
  void begin(const DeviceConfig& cfg, const std::string& clientId, const std::string& prefix,
             net::IHostResolver* resolver, net::LinkStatus* status);

  void setOnOnline(std::function<void()> cb) { onOnline_ = std::move(cb); }

  bool tick(uint32_t nowMs);

  PubSubClient* client() { return client_; }
  bool enabled() const { return enabled_; }
  bool online() const { return client_ != nullptr && client_->connected(); }

 private:
  static constexpr uint16_t kReresolveAfterFailures = 3;

  bool connectNow();
  void noteFailure(uint32_t nowMs, net::LinkError why, int state);
  static net::LinkError mapState(int pubSubState);

  WiFiClient wifi_;
  PubSubClient* client_ = nullptr;
  net::IHostResolver* resolver_ = nullptr;
  net::LinkStatus* status_ = nullptr;
  net::ConnectionBackoff backoff_;
  std::function<void()> onOnline_;

  std::string host_, user_, pass_, prefix_, clientId_;
  IPAddress address_;
  uint16_t port_ = 1883;
  uint16_t failuresAgainstAddress_ = 0;
  bool enabled_ = false;
  bool haveServer_ = false;
  bool wasConnected_ = false;
  bool sawWifi_ = false;
  net::LinkError loggedError_ = net::LinkError::None;
};

}
