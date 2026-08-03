#pragma once

#include <cstddef>
#include <cstdint>

namespace awtrix {
namespace mp3 {

// MSB-first bit reader over the main-data buffer. Reading past the end latches overrun_ and parks
// at the end rather than failing per call, so callers check overrun() once per granule.
class BitReader {
 public:
  BitReader(const uint8_t* data, std::size_t bytes)
      : data_(data), totalBits_(data == nullptr ? 0 : bytes * 8) {}

  uint32_t read(unsigned bits) {
    if (bits == 0) return 0;
    if (bits > bitsLeft()) {
      overrun_ = true;
      pos_ = totalBits_;
      return 0;
    }
    const uint32_t v = peek(bits);
    pos_ += bits;
    return v;
  }

  // Assembles up to 5 source bytes into a 64-bit accumulator, so a 32-bit field may straddle any
  // byte boundary.
  uint32_t peek(unsigned bits) const {
    if (bits == 0 || bits > 32 || bits > bitsLeft()) return 0;
    const std::size_t firstByte = pos_ >> 3;
    const unsigned bitOffset = static_cast<unsigned>(pos_ & 7);
    const unsigned needed = (bitOffset + bits + 7) / 8;
    uint64_t acc = 0;
    for (unsigned i = 0; i < needed; ++i) acc = (acc << 8) | data_[firstByte + i];
    acc >>= (needed * 8 - bitOffset - bits);
    const uint64_t mask = (bits == 32) ? 0xFFFFFFFFull : ((1ull << bits) - 1);
    return static_cast<uint32_t>(acc & mask);
  }

  void skip(std::size_t bits) {
    if (bits > bitsLeft()) {
      overrun_ = true;
      pos_ = totalBits_;
      return;
    }
    pos_ += bits;
  }

  void seekBits(std::size_t bitPosition) {
    pos_ = bitPosition > totalBits_ ? totalBits_ : bitPosition;
  }

  std::size_t bitPos() const { return pos_; }
  std::size_t bitsLeft() const { return totalBits_ - pos_; }
  bool exhausted() const { return pos_ >= totalBits_; }

  bool overrun() const { return overrun_; }

 private:
  const uint8_t* data_ = nullptr;
  std::size_t totalBits_ = 0;
  std::size_t pos_ = 0;
  bool overrun_ = false;
};

}
}
