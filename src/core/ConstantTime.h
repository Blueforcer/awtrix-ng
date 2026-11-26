#pragma once

#include <cstddef>
#include <cstdint>

namespace awtrix {

inline bool constantTimeEqual(const void* a, const void* b, std::size_t length) {
  const auto* x = static_cast<const uint8_t*>(a);
  const auto* y = static_cast<const uint8_t*>(b);
  uint8_t diff = 0;
  for (std::size_t i = 0; i < length; ++i) diff |= static_cast<uint8_t>(x[i] ^ y[i]);
  return diff == 0;
}

}
