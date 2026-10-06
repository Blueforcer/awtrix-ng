#pragma once

#include <Client.h>
#include <functional>
#include <string>
#include "transport/net/HostResolver.h"

namespace awtrix {
// Optional platform transport. Broker protocol, retries and command dispatch stay
// in MqttLink/MqttService. A pending handshake owns the PubSubClient exclusively.
class IMqttTransport {
 public:
  virtual ~IMqttTransport() = default;
  virtual Client& socket() = 0;
  virtual void setPeerName(const std::string& host) = 0;
  virtual net::ResolveState connectStep(std::function<bool()> handshake) = 0;
  virtual void shutdown() = 0;
};
}
