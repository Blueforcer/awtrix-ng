#include "core/audio/Mp3Huffman.h"

namespace awtrix {
namespace mp3 {

namespace {

// Canonical Huffman decode: shift in one bit at a time and, at each code length that has codes,
// binary-search that length's slice of the sorted code array. No decode tree, so no RAM cost.
bool decodeSymbol(BitReader& bits, const HuffTable& table, uint8_t& symbol) {
  uint32_t code = 0;
  for (unsigned length = 1; length <= kMaxCodeLength; ++length) {
    code = (code << 1) | bits.read(1);
    if (bits.overrun()) return false;

    const unsigned count = table.count[length];
    if (count == 0) continue;

    unsigned low = table.first[length];
    unsigned high = low + count;
    while (low < high) {
      const unsigned mid = low + (high - low) / 2;
      if (table.codes[mid] < code) {
        low = mid + 1;
      } else if (table.codes[mid] > code) {
        high = mid;
      } else {
        symbol = table.symbols[mid];
        return true;
      }
    }
  }
  return false;
}

}

bool decodePair(BitReader& bits, const HuffTable& table, Pair& out) {
  // A null table is one of the unused table slots: it yields zeros and consumes no bits.
  if (table.codes == nullptr) {
    out = Pair{};
    return true;
  }
  uint8_t symbol = 0;
  if (!decodeSymbol(bits, table, symbol)) return false;
  out.x = (symbol >> 4) & 0x0F;
  out.y = symbol & 0x0F;
  return true;
}

bool decodeQuad(BitReader& bits, const HuffTable& table, Quad& out) {
  if (table.codes == nullptr) {
    out = Quad{};
    return true;
  }
  uint8_t symbol = 0;
  if (!decodeSymbol(bits, table, symbol)) return false;
  out.v = (symbol >> 3) & 1;
  out.w = (symbol >> 2) & 1;
  out.x = (symbol >> 1) & 1;
  out.y = symbol & 1;
  return true;
}

}
}
