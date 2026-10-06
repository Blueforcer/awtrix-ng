#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace awtrix::images {

// A piece of a zlib stream. A PNG spreads its stream over several chunks, which are read where
// they are.
struct ByteSpan {
  const uint8_t* data = nullptr;
  std::size_t size = 0;
};

using InflateSink = std::function<bool(const uint8_t* bytes, std::size_t count)>;

// Unpacks a complete zlib stream (RFC 1950/1951), given as consecutive pieces, in one pass and
// hands the output on in pieces, so only a 64 KB window is held however large the result. The
// sink returns false to stop. False for a damaged stream, a preset dictionary, or a stop.
bool inflateZlib(const ByteSpan* pieces, std::size_t count, const InflateSink& sink);
inline bool inflateZlib(const uint8_t* data, std::size_t size, const InflateSink& sink) {
  const ByteSpan whole{data, size};
  return inflateZlib(&whole, 1, sink);
}

}
