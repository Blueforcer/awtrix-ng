#pragma once

#include "core/audio/Mp3Bitstream.h"
#include "core/audio/Mp3HuffmanTables.h"

namespace awtrix {
namespace mp3 {

struct Pair {
  int x = 0;
  int y = 0;
};

struct Quad {
  int v = 0;
  int w = 0;
  int x = 0;
  int y = 0;
};

inline constexpr unsigned kMaxCodeLength = 19;

bool decodePair(BitReader& bits, const HuffTable& table, Pair& out);

bool decodeQuad(BitReader& bits, const HuffTable& table, Quad& out);

}
}
