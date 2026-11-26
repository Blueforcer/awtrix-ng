#include "core/remote/PairingKey.h"

#include <cstring>

#include "core/ConstantTime.h"

namespace awtrix {
namespace remote {

namespace {
int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}

bool PairingKey::parse(const std::string& hex, PairingKey& out) {
  if (hex.size() != sizeof(out.bytes) * 2) return false;
  for (std::size_t i = 0; i < sizeof(out.bytes); ++i) {
    const int hi = nibble(hex[2 * i]);
    const int lo = nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out.bytes[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  return true;
}

wire::Signer signerFor(const PairingKey& key, HmacSha256 hmac) {
  return [key, hmac](const uint8_t* data, std::size_t length, uint8_t* tag) {
    uint8_t full[32] = {};
    hmac(key.bytes, sizeof(key.bytes), data, length, full);
    std::memcpy(tag, full, wire::kTagBytes);
  };
}

bool signedBy(const wire::Packet& packet, const PairingKey& key, HmacSha256 hmac) {
  if (!packet.tag || packet.signedBytes == 0) return false;
  const uint8_t* data = packet.tag - packet.signedBytes;
  uint8_t full[32] = {};
  if (!hmac(key.bytes, sizeof(key.bytes), data, packet.signedBytes, full)) return false;
  return constantTimeEqual(full, packet.tag, wire::kTagBytes);
}

}
}
