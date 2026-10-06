#include "platform/linux/images/PictureFit.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace awtrix::images {
namespace {

constexpr double kLinearMax = 65535.0;

const std::array<uint16_t, 256>& linearOf() {
  static const std::array<uint16_t, 256> table = [] {
    std::array<uint16_t, 256> t{};
    for (int i = 0; i < 256; ++i) {
      const double c = i / 255.0;
      const double l = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
      t[i] = static_cast<uint16_t>(std::lround(l * kLinearMax));
    }
    return t;
  }();
  return table;
}

uint32_t srgbOf(uint64_t linear) {
  const double l = std::min(1.0, linear / kLinearMax);
  const double c = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
  return static_cast<uint32_t>(std::lround(std::clamp(c, 0.0, 1.0) * 255.0));
}

int64_t divRound(int64_t a, int64_t b) { return (a + b / 2) / b; }

}

// Shrinking, every source pixel of the cut feeds the one target pixel it falls in, so each target
// pixel gets at least one. Growing, every target pixel takes the source pixel under its centre.
std::vector<PictureFit::Span> PictureFit::spans(int source, int offset, int cut, int target) {
  std::vector<Span> out(static_cast<std::size_t>(source));
  if (cut >= target) {
    for (int s = offset; s < offset + cut; ++s)
      out[s].first = out[s].last = static_cast<int16_t>(static_cast<int64_t>(s - offset) * target / cut);
    return out;
  }
  for (int t = 0; t < target; ++t) {
    Span& span = out[offset + static_cast<int>((2LL * t + 1) * cut / (2LL * target))];
    if (span.last < span.first) span.first = static_cast<int16_t>(t);
    span.last = static_cast<int16_t>(t);
  }
  return out;
}

bool PictureFit::begin(int sourceWidth, int sourceHeight, int width, int height) {
  if (sourceWidth <= 0 || sourceHeight <= 0 || sourceWidth > kMaxSourceSide ||
      sourceHeight > kMaxSourceSide || width <= 0 || height <= 0 || width > kMaxSide ||
      height > kMaxSide)
    return false;
  int64_t cutWidth = sourceWidth, cutHeight = sourceHeight;
  if (static_cast<int64_t>(sourceWidth) * height > static_cast<int64_t>(sourceHeight) * width)
    cutWidth = std::max<int64_t>(1, divRound(static_cast<int64_t>(sourceHeight) * width, height));
  else
    cutHeight = std::max<int64_t>(1, divRound(static_cast<int64_t>(sourceWidth) * height, width));
  width_ = width;
  height_ = height;
  columns_ = spans(sourceWidth, static_cast<int>((sourceWidth - cutWidth) / 2), static_cast<int>(cutWidth), width);
  rows_ = spans(sourceHeight, static_cast<int>((sourceHeight - cutHeight) / 2), static_cast<int>(cutHeight), height);
  cells_.assign(static_cast<std::size_t>(width) * height, Cell{});
  return true;
}

void PictureFit::add(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
  if (x < 0 || y < 0 || static_cast<std::size_t>(x) >= columns_.size() ||
      static_cast<std::size_t>(y) >= rows_.size())
    return;
  const Span& column = columns_[x];
  const Span& row = rows_[y];
  if (column.last < column.first || row.last < row.first) return;
  const auto& linear = linearOf();
  const uint32_t lr = linear[r] * alpha / 255u;
  const uint32_t lg = linear[g] * alpha / 255u;
  const uint32_t lb = linear[b] * alpha / 255u;
  for (int ty = row.first; ty <= row.last; ++ty)
    for (int tx = column.first; tx <= column.last; ++tx) {
      Cell& cell = cells_[static_cast<std::size_t>(ty) * width_ + tx];
      cell.r += lr;
      cell.g += lg;
      cell.b += lb;
      ++cell.count;
    }
}

bool PictureFit::finish(media::PodBuffer<uint32_t>& out) {
  if (!out.resize(cells_.size(), cells_.size())) return false;
  for (std::size_t i = 0; i < cells_.size(); ++i) {
    const Cell& c = cells_[i];
    const uint32_t count = c.count ? c.count : 1;
    const uint64_t r = (c.r + count / 2) / count;
    const uint64_t g = (c.g + count / 2) / count;
    const uint64_t b = (c.b + count / 2) / count;
    out[i] = srgbOf(r) << 16 | srgbOf(g) << 8 | srgbOf(b);
  }
  return true;
}

}
