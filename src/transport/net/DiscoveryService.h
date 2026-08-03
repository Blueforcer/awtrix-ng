#pragma once

#include <string>

#include "transport/net/UdpSocket.h"

namespace awtrix {

class DiscoveryService {
 public:
  void begin(const std::string& hostname, int webPort);
  void tick();

 private:
  UdpSocket udp_;
  std::string reply_;
  bool active_ = false;
};

}
