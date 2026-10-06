#pragma once

#include <WiFiClient.h>
#include "platform/linux/mqtt/IMqttTransport.h"

namespace awtrix {
class MqttSocket {
 public:
  using Options = IMqttTransport*;
  void begin(Options transport, const std::string& host);
  void shutdown();
  Client& socket();
  bool connecting() const { return connecting_; }
  net::ResolveState connectStep(std::function<bool()> connect);
 private:
  WiFiClient wifi_;
  IMqttTransport* transport_ = nullptr;
  bool connecting_ = false;
};
}
