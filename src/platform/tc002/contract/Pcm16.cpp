#include "platform/tc002/contract/Pcm16.h"

#include "core/payload/Base64.h"

namespace awtrix::tc002 {
std::string encodePcm16(const std::vector<int16_t>& samples) {
  std::string out;
  out.reserve((samples.size() * 2 + 2) / 3 * 4);
  uint32_t bits = 0;
  unsigned count = 0;
  for (const auto sample : samples) {
    for (const unsigned shift : {0u, 8u}) {
      bits = (bits << 8) | ((static_cast<uint16_t>(sample) >> shift) & 255u);
      count += 8;
      while (count >= 6) { count -= 6; out += base64::kAlphabet[(bits >> count) & 63u]; }
    }
  }
  if (count) out += base64::kAlphabet[(bits << (6 - count)) & 63u];
  while (out.size() % 4) out += '=';
  return out;
}

bool decodePcm16(std::string_view encoded, std::vector<int16_t>& samples) {
  // Canonical RFC 4648 padding and zero pad bits, without encoding the PCM
  // a second time. The decoder below checks every non-padding character.
  if (encoded.size() % 4) return false;
  const std::size_t length = base64::unpadded(encoded.data(), encoded.size());
  const std::size_t padding = encoded.size() - length;
  if (padding > 2 || (padding && !length)) return false;
  if (padding) {
    const int tail = base64::sextet(encoded[length - 1]);
    if (tail < 0 || (tail & (padding == 1 ? 3 : 15))) return false;
  }
  std::vector<uint8_t> bytes;
  if (!base64::decode(encoded.data(), encoded.size(), bytes) || bytes.size() % 2) return false;
  std::vector<int16_t> decoded;
  decoded.reserve(bytes.size() / 2);
  for (std::size_t i = 0; i < bytes.size(); i += 2) {
    const int value = bytes[i] | (bytes[i + 1] << 8);
    decoded.push_back(static_cast<int16_t>(value < 32768 ? value : value - 65536));
  }
  samples = std::move(decoded);
  return true;
}
}
