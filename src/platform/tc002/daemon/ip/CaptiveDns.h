#pragma once
#include "platform/tc002/contract/tc002_layout.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace awtrix {
namespace tc002d {
namespace ip {

constexpr uint32_t kAccessPointAddress = [] {
  uint32_t address = 0, octet = 0;
  for (char c : TC002_AP_ADDRESS) {
    if (c == '.' || c == '\0') { address = (address << 8) | octet; octet = 0; }
    else octet = octet * 10 + static_cast<unsigned>(c - '0');
  }
  return address;
}();
constexpr uint8_t kAccessPointPrefix = 24;
constexpr std::size_t kMaxCaptiveDnsPacket = 1232;

std::vector<uint8_t> captiveDnsReply(const uint8_t* packet, std::size_t size);

class CaptiveDns {
 public:
  ~CaptiveDns();
  CaptiveDns() = default;
  CaptiveDns(const CaptiveDns&) = delete;
  CaptiveDns& operator=(const CaptiveDns&) = delete;
  bool start(const std::string& interfaceName, std::string& error);
  void stop();
  int fd() const { return fd_; }
  void onReadable();

 private:
  int fd_ = -1;
};

}
}
}
