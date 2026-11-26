#pragma once

#include <cstdint>

#include "core/mirror/Mirror.h"
#include "persistence/DeviceConfig.h"
#include "transport/net/HostLookup.h"
#include "transport/net/UdpPort.h"

namespace awtrix {

// Carries display mirroring over UDP: keeps the port open while the device is online and mirroring
// is configured, feeds every datagram to the mirror and looks the followed clock up by name.
class MirrorLink final {
 public:
  MirrorLink() : mirror_(udp_) {}

  mirror::Mirror& mirror() { return mirror_; }
  void begin(int width, int height, mirror::Status& status) {
    mirror_.begin(width, height, status);
  }
  void configure(const DeviceConfig& config);
  void tick(int64_t nowMs, bool online);

 private:
  void lookUp(int64_t nowMs);

  net::UdpPort udp_;
  net::HostLookup lookup_;
  bool haveSource_ = false;
  mirror::Mirror mirror_;
};

}
