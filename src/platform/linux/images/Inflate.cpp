#include "platform/linux/images/Inflate.h"

#include <vector>

namespace awtrix::images {
namespace {

constexpr int kMaxBits = 15;
constexpr int kLengthCodes = 286;
constexpr int kDistanceCodes = 30;
constexpr std::size_t kWindow = 1u << 16;

// Canonical Huffman code: how many codes each length has, and the symbols in code order.
struct Huffman {
  uint16_t count[kMaxBits + 1];
  uint16_t symbol[288];
};

// 0 for a complete code, more for an incomplete one, less for an over-subscribed one.
int construct(Huffman& h, const uint8_t* lengths, int n) {
  for (int len = 0; len <= kMaxBits; ++len) h.count[len] = 0;
  for (int s = 0; s < n; ++s) ++h.count[lengths[s]];
  if (h.count[0] == n) return 0;
  int left = 1;
  for (int len = 1; len <= kMaxBits; ++len) {
    left <<= 1;
    left -= h.count[len];
    if (left < 0) return left;
  }
  uint16_t offsets[kMaxBits + 1];
  offsets[1] = 0;
  for (int len = 1; len < kMaxBits; ++len) offsets[len + 1] = static_cast<uint16_t>(offsets[len] + h.count[len]);
  for (int s = 0; s < n; ++s)
    if (lengths[s]) h.symbol[offsets[lengths[s]]++] = static_cast<uint16_t>(s);
  return left;
}

class Inflater {
 public:
  Inflater(const ByteSpan* pieces, std::size_t count, const InflateSink& sink)
      : pieces_(pieces), count_(count), sink_(sink), window_(kWindow) {}

  bool run() {
    uint8_t method = 0, flags = 0;
    if (!next(method) || !next(flags) || (method & 0x0F) != 8 || (method >> 4) > 7 ||
        ((method << 8) | flags) % 31 != 0 || (flags & 0x20))
      return false;
    int last = 0;
    do {
      last = bits(1);
      const int type = bits(2);
      const bool ok = type == 0 ? stored() : type == 1 ? fixed() : type == 2 ? dynamic() : false;
      if (!ok || overrun_ || stopped_) return false;
    } while (!last);
    flush();
    return !stopped_;
  }

 private:
  bool next(uint8_t& byte) {
    while (piece_ < count_ && offset_ >= pieces_[piece_].size) {
      ++piece_;
      offset_ = 0;
    }
    if (piece_ == count_) return false;
    byte = pieces_[piece_].data[offset_++];
    return true;
  }

  int bits(int need) {
    uint32_t value = bitBuffer_;
    while (bitCount_ < need) {
      uint8_t byte = 0;
      if (!next(byte)) {
        overrun_ = true;
        return 0;
      }
      value |= static_cast<uint32_t>(byte) << bitCount_;
      bitCount_ += 8;
    }
    bitBuffer_ = value >> need;
    bitCount_ -= need;
    return static_cast<int>(value & ((1u << need) - 1));
  }

  void put(uint8_t byte) {
    window_[out_++] = byte;
    ++total_;
    if (out_ == kWindow) flush();
  }

  void flush() {
    if (out_ > flushed_ && !sink_(window_.data() + flushed_, out_ - flushed_)) stopped_ = true;
    if (out_ == kWindow) out_ = 0;
    flushed_ = out_;
  }

  int decode(const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
      code |= bits(1);
      const int count = h.count[len];
      if (code - count < first) return h.symbol[index + (code - first)];
      index += count;
      first = (first + count) << 1;
      code <<= 1;
      if (overrun_) return -1;
    }
    return -1;
  }

  bool stored() {
    bitBuffer_ = 0;
    bitCount_ = 0;
    uint8_t header[4];
    for (uint8_t& byte : header)
      if (!next(byte)) return false;
    const unsigned length = header[0] | header[1] << 8;
    if (length != (~(header[2] | header[3] << 8) & 0xFFFFu)) return false;
    for (unsigned i = 0; i < length && !stopped_; ++i) {
      uint8_t byte = 0;
      if (!next(byte)) return false;
      put(byte);
    }
    return true;
  }

  bool codes(const Huffman& lengths, const Huffman& distances) {
    static const uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                             31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                             2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t kDistanceBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                               33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                               1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                               6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
      int symbol = decode(lengths);
      if (symbol < 0) return false;
      if (symbol == 256) return true;
      if (symbol < 256) {
        put(static_cast<uint8_t>(symbol));
      } else {
        symbol -= 257;
        if (symbol >= 29) return false;
        int length = kLengthBase[symbol] + bits(kLengthExtra[symbol]);
        symbol = decode(distances);
        if (symbol < 0 || symbol >= 30) return false;
        const std::size_t distance = kDistanceBase[symbol] + bits(kDistanceExtra[symbol]);
        if (distance > total_) return false;
        while (length--) put(window_[(out_ - distance) & (kWindow - 1)]);
      }
      if (overrun_ || stopped_) return false;
    }
  }

  bool fixed() {
    Huffman lengths, distances;
    uint8_t sizes[288];
    int s = 0;
    for (; s < 144; ++s) sizes[s] = 8;
    for (; s < 256; ++s) sizes[s] = 9;
    for (; s < 280; ++s) sizes[s] = 7;
    for (; s < 288; ++s) sizes[s] = 8;
    construct(lengths, sizes, 288);
    for (s = 0; s < kDistanceCodes; ++s) sizes[s] = 5;
    construct(distances, sizes, kDistanceCodes);
    return codes(lengths, distances);
  }

  bool dynamic() {
    static const uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int lengthCount = bits(5) + 257;
    const int distanceCount = bits(5) + 1;
    const int codeCount = bits(4) + 4;
    if (overrun_ || lengthCount > kLengthCodes || distanceCount > kDistanceCodes) return false;
    uint8_t sizes[kLengthCodes + kDistanceCodes] = {};
    for (int i = 0; i < codeCount; ++i) sizes[kOrder[i]] = static_cast<uint8_t>(bits(3));
    Huffman lengths, distances;
    if (construct(lengths, sizes, 19) != 0) return false;
    int index = 0;
    while (index < lengthCount + distanceCount) {
      int symbol = decode(lengths);
      if (symbol < 0) return false;
      if (symbol < 16) {
        sizes[index++] = static_cast<uint8_t>(symbol);
        continue;
      }
      uint8_t repeated = 0;
      if (symbol == 16) {
        if (index == 0) return false;
        repeated = sizes[index - 1];
        symbol = 3 + bits(2);
      } else {
        symbol = symbol == 17 ? 3 + bits(3) : 11 + bits(7);
      }
      if (overrun_ || index + symbol > lengthCount + distanceCount) return false;
      while (symbol--) sizes[index++] = repeated;
    }
    if (sizes[256] == 0) return false;
    // Only a code with a single length-1 entry may be incomplete.
    int left = construct(lengths, sizes, lengthCount);
    if (left < 0 || (left > 0 && lengthCount != lengths.count[0] + lengths.count[1])) return false;
    left = construct(distances, sizes + lengthCount, distanceCount);
    if (left < 0 || (left > 0 && distanceCount != distances.count[0] + distances.count[1])) return false;
    return codes(lengths, distances);
  }

  const ByteSpan* pieces_;
  std::size_t count_;
  std::size_t piece_ = 0;
  std::size_t offset_ = 0;
  uint32_t bitBuffer_ = 0;
  int bitCount_ = 0;
  bool overrun_ = false;
  bool stopped_ = false;
  const InflateSink& sink_;
  std::vector<uint8_t> window_;
  std::size_t out_ = 0;
  std::size_t flushed_ = 0;
  std::size_t total_ = 0;
};

}

bool inflateZlib(const ByteSpan* pieces, std::size_t count, const InflateSink& sink) {
  return Inflater(pieces, count, sink).run();
}

}
