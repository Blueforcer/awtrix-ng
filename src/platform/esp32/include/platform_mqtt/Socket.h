#pragma once

#include <WiFiClient.h>
#include "transport/net/HostResolver.h"

namespace awtrix {
class MqttSocket {
 public:
  struct Options {};
  void begin(Options, const std::string&) {}
  void shutdown() { wifi_.stop(); }
  Client& socket() { return wifi_; }
  static constexpr bool connecting() { return false; }
  template <typename Connect>
  net::ResolveState connectStep(Connect connect) {
    return connect() ? net::ResolveState::Ready : net::ResolveState::Failed;
  }
 private:
  WiFiClient wifi_;
};
}
