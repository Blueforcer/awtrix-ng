#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
class IPAddress {
 public:
  explicit IPAddress(uint32_t ip = 0) : ip_(ip) {}
  operator uint32_t() const { return ip_; }
  uint8_t operator[](int i) const { return static_cast<uint8_t>(ip_ >> (8 * i)); }
 private:
  uint32_t ip_;
};
class WiFiUDP {
 public:
  inline static std::vector<uint8_t> incoming, sent;
  inline static size_t pos = 0;
  inline static uint16_t listen = 0, destination = 0;
  inline static uint32_t peer = 42, recipient = 0;
  inline static bool consumed = false, bindOk = true, sendOk = true;
  int begin(uint16_t p) { listen = p; return bindOk; }
  void stop() { listen = 0; }
  int parsePacket() { if (consumed) return 0; consumed = true; pos = 0; return incoming.size(); }
  IPAddress remoteIP() { return IPAddress(peer); }
  int read(unsigned char* out, size_t cap) {
    const auto n = std::min(cap, incoming.size() - pos);
    std::copy_n(incoming.data() + pos, n, out); pos += n; return n;
  }
  int read() { return pos < incoming.size() ? incoming[pos++] : -1; }
  int available() { return incoming.size() - pos; }
  int beginPacket(IPAddress ip, uint16_t p) { recipient = ip; destination = p; return sendOk; }
  size_t write(const uint8_t* data, size_t len) { sent.assign(data, data + len); return len; }
  int endPacket() { return sendOk; }
};
