#include "core/render/Fingerprint.h"

#include <cstddef>

namespace awtrix {

namespace {
uint32_t mix(uint32_t hash, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    hash ^= (value >> shift) & 0xFFu;
    hash *= 16777619u;
  }
  return hash;
}
}

void Fingerprint::add(uint32_t value) { hash_ = mix(hash_, value); }

void Fingerprint::add(std::string_view text) {
  for (char c : text) hash_ = mix(hash_, static_cast<uint8_t>(c));
  hash_ = mix(hash_, static_cast<uint32_t>(text.size()));
}

void Fingerprint::add(const Canvas& canvas) {
  const uint32_t* pixels = canvas.data();
  for (std::size_t i = 0, n = canvas.size(); i < n; ++i) hash_ = mix(hash_, pixels[i]);
}

}
