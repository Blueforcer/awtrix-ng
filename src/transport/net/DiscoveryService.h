#pragma once

#include <string>

#include "transport/net/UdpSocket.h"

namespace awtrix {

class ScriptHostLink;

class DiscoveryService {
 public:
  void begin(const std::string& hostname, int webPort);
  // Gets every script host datagram ("AWRH...") that reaches the discovery port, with its sender.
  void setScriptHost(ScriptHostLink* link) { scriptHost_ = link; }
  void tick();

 private:
  UdpSocket udp_;
  std::string reply_;
  ScriptHostLink* scriptHost_ = nullptr;
  bool active_ = false;
};

}
