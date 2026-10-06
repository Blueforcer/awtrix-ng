#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/mirror/Mirror.h"
#include "core/net/Datagram.h"
#include "persistence/DeviceConfig.h"
#include "transport/net/HostResolver.h"
#include "transport/net/UdpSocket.h"

namespace awtrix {

// Carries display mirroring over UDP: keeps the port open while the device is online and mirroring
// is configured, feeds every datagram to the mirror and looks the followed clock up by name.
class MirrorLink final : private net::IDatagramSink {
 public:
  MirrorLink() : mirror_(*this) {}

  mirror::Mirror& mirror() { return mirror_; }
  void begin(int width, int height, mirror::Status& status) {
    mirror_.begin(width, height, status);
  }
  void configure(const DeviceConfig& config);
  void tick(int64_t nowMs, bool online);

 private:
  static constexpr int64_t kOpenRetryMs = 5000;
  static constexpr int64_t kLookupRetryMs = 10000;
  static constexpr int64_t kRelookupMs = 30000;

  bool send(const net::Endpoint& to, const uint8_t* data, std::size_t length) override;
  void drain(int64_t nowMs);
  void lookUp(int64_t nowMs);

  UdpSocket socket_;
  std::unique_ptr<uint8_t[]> rx_;
  std::unique_ptr<net::IHostResolver> resolver_;
  std::string host_;
  net::Endpoint source_;
  bool haveSource_ = false;
  bool relooking_ = false;
  int64_t openRetryAtMs_ = 0;
  int64_t lookupAtMs_ = 0;
  int64_t resolvedAtMs_ = 0;
  mirror::Mirror mirror_;
};

}
