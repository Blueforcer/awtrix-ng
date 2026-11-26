#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/remote/HostAddress.h"
#include "core/remote/RemoteWire.h"

namespace awtrix {
namespace remote {

// HMAC-SHA256 as the platform provides it: out takes 32 bytes.
using HmacSha256 = bool (*)(const uint8_t* key, std::size_t keyLength, const uint8_t* data,
                            std::size_t length, uint8_t* out);

// The 32 bytes a clock and its script host share since pairing.
struct PairingKey {
  uint8_t bytes[32] = {};
  static bool parse(const std::string& hex, PairingKey& out);
};

wire::Signer signerFor(const PairingKey& key, HmacSha256 hmac);
// Whether a decoded Hello or signed Offer carries the tag of this key.
bool signedBy(const wire::Packet& packet, const PairingKey& key, HmacSha256 hmac);

}
}
