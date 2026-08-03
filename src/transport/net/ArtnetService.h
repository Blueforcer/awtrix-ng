#pragma once

#include <cstdint>
#include <vector>

#include "core/render/Canvas.h"
#include "transport/net/UdpSocket.h"

namespace awtrix {

class ArtnetService {
 public:
  void begin();
  void end();
  bool tick(Canvas& out, int64_t nowMs);

 private:
  void sendPollReply();

  UdpSocket udp_;
  bool active_ = false;
  int64_t lastMs_ = -100000;
  std::vector<uint32_t> frame_;
};

}
