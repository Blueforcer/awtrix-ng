#pragma once


#include <cstdint>
#include <cstdio>
#include <string>

// Octets are packed low byte first, matching lwIP and Arduino, so operator[](0) gives back 'a' and
// the uint32_t conversion is already in the order a sockaddr_in wants.
class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
      : v_(static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) |
           (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24)) {}
  explicit IPAddress(uint32_t v) : v_(v) {}

  operator uint32_t() const { return v_; }
  uint8_t operator[](int i) const { return static_cast<uint8_t>(v_ >> (8 * i)); }

  std::string toString() const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (*this)[0], (*this)[1], (*this)[2], (*this)[3]);
    return std::string(buf);
  }

 private:
  uint32_t v_ = 0;
};
