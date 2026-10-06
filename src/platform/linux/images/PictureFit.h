#pragma once

#include <cstdint>
#include <vector>

#include "media/PodBuffer.h"

namespace awtrix::images {

// Fits a picture that arrives pixel by pixel to exactly width x height. Its middle is cut to the
// target's shape. Along a side the cut is longer than the target, each target pixel averages the
// pixels under it in linear light; along a side it is shorter, it repeats the nearest one.
class PictureFit {
 public:
  static constexpr int kMaxSide = 128;
  static constexpr int kMaxSourceSide = 16384;

  // False for an empty or oversized source or target.
  bool begin(int sourceWidth, int sourceHeight, int width, int height);
  // Every source pixel, in any order. alpha 0 is transparent, which shows as black.
  void add(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha = 255);
  // 0xRRGGBB rows; false only without memory for them.
  bool finish(media::PodBuffer<uint32_t>& out);

 private:
  // The target pixels one source pixel feeds along one side; empty outside the cut.
  struct Span {
    int16_t first = 0;
    int16_t last = -1;
  };
  struct Cell {
    uint64_t r = 0, g = 0, b = 0;
    uint32_t count = 0;
  };
  static std::vector<Span> spans(int source, int offset, int cut, int target);

  int width_ = 0;
  int height_ = 0;
  std::vector<Span> columns_, rows_;
  std::vector<Cell> cells_;
};

}
