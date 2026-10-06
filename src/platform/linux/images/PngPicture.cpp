#include "platform/linux/images/PngPicture.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "platform/linux/images/Inflate.h"
#include "platform/linux/images/PictureFit.h"

namespace awtrix::images {
namespace {

// The maximum number of pixels a PNG may unpack to.
constexpr uint64_t kMaxPixels = 2048u * 2048u;

uint32_t bigEndian(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

int channelsOf(int color) {
  switch (color) {
    case 0: return 1;
    case 2: return 3;
    case 3: return 1;
    case 4: return 2;
    case 6: return 4;
    default: return 0;
  }
}

bool allowedDepth(int color, int depth) {
  if (color == 0) return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
  if (color == 3) return depth == 1 || depth == 2 || depth == 4 || depth == 8;
  return depth == 8 || depth == 16;
}

struct Png {
  uint32_t width = 0, height = 0;
  int depth = 0, color = 0, channels = 0;
  uint8_t palette[256][4] = {};
  std::size_t paletteSize = 0;
  bool keyed = false;
  unsigned key[3] = {};

  unsigned sample(const uint8_t* row, std::size_t index) const {
    if (depth == 16) return static_cast<unsigned>(row[index * 2]) << 8 | row[index * 2 + 1];
    if (depth == 8) return row[index];
    const std::size_t bit = index * depth;
    return (row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
  }
  uint8_t eight(unsigned value) const {
    if (depth == 16) return static_cast<uint8_t>(value >> 8);
    if (depth == 8) return static_cast<uint8_t>(value);
    return static_cast<uint8_t>(value * 255 / ((1u << depth) - 1));
  }

  void emit(const uint8_t* row, int y, PictureFit& fit) const {
    for (uint32_t x = 0; x < width; ++x) {
      const std::size_t first = static_cast<std::size_t>(x) * channels;
      unsigned v[4] = {};
      for (int c = 0; c < channels; ++c) v[c] = sample(row, first + c);
      uint8_t r, g, b, a = 255;
      switch (color) {
        case 3: {
          static const uint8_t kBlack[4] = {0, 0, 0, 255};
          const uint8_t* entry = v[0] < paletteSize ? palette[v[0]] : kBlack;
          r = entry[0], g = entry[1], b = entry[2], a = entry[3];
          break;
        }
        case 0:
        case 4:
          r = g = b = eight(v[0]);
          if (color == 4) a = eight(v[1]);
          else if (keyed && v[0] == key[0]) a = 0;
          break;
        default:
          r = eight(v[0]), g = eight(v[1]), b = eight(v[2]);
          if (color == 6) a = eight(v[3]);
          else if (keyed && v[0] == key[0] && v[1] == key[1] && v[2] == key[2]) a = 0;
          break;
      }
      fit.add(static_cast<int>(x), y, r, g, b, a);
    }
  }
};

uint8_t paeth(int a, int b, int c) {
  const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
  return static_cast<uint8_t>(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

// Undoes a row's filter in place; the filter type is its first byte.
bool unfilter(uint8_t* row, const uint8_t* previous, std::size_t stride, std::size_t step) {
  uint8_t* cur = row + 1;
  const uint8_t* up = previous + 1;
  switch (row[0]) {
    case 0: return true;
    case 1:
      for (std::size_t i = step; i < stride; ++i) cur[i] = static_cast<uint8_t>(cur[i] + cur[i - step]);
      return true;
    case 2:
      for (std::size_t i = 0; i < stride; ++i) cur[i] = static_cast<uint8_t>(cur[i] + up[i]);
      return true;
    case 3:
      for (std::size_t i = 0; i < stride; ++i)
        cur[i] = static_cast<uint8_t>(cur[i] + ((i >= step ? cur[i - step] : 0) + up[i]) / 2);
      return true;
    case 4:
      for (std::size_t i = 0; i < stride; ++i)
        cur[i] = static_cast<uint8_t>(cur[i] + paeth(i >= step ? cur[i - step] : 0, up[i], i >= step ? up[i - step] : 0));
      return true;
    default: return false;
  }
}

}

DecodeFailure decodePng(const uint8_t* data, std::size_t size, int width, int height, media::RemoteImage& out) {
  static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (size < 8 || std::memcmp(data, kSignature, 8) != 0) return DecodeFailure::Format;
  Png png;
  bool header = false;
  std::vector<ByteSpan> compressed;
  for (std::size_t at = 8; at + 12 <= size;) {
    const uint32_t length = bigEndian(data + at);
    if (length > size - at - 12) return DecodeFailure::Format;
    const char* type = reinterpret_cast<const char*>(data + at + 4);
    const uint8_t* chunk = data + at + 8;
    at += 12 + length;
    if (std::memcmp(type, "IHDR", 4) == 0) {
      if (length != 13 || chunk[10] != 0 || chunk[11] != 0 || chunk[12] != 0) return DecodeFailure::Format;
      png.width = bigEndian(chunk);
      png.height = bigEndian(chunk + 4);
      png.depth = chunk[8];
      png.color = chunk[9];
      png.channels = channelsOf(png.color);
      if (!png.channels || !allowedDepth(png.color, png.depth)) return DecodeFailure::Format;
      header = true;
    } else if (!header) {
      return DecodeFailure::Format;
    } else if (std::memcmp(type, "PLTE", 4) == 0) {
      if (length % 3 || length / 3 > 256) return DecodeFailure::Format;
      png.paletteSize = length / 3;
      for (std::size_t i = 0; i < png.paletteSize; ++i) {
        std::memcpy(png.palette[i], chunk + i * 3, 3);
        png.palette[i][3] = 255;
      }
    } else if (std::memcmp(type, "tRNS", 4) == 0) {
      if (png.color == 3) {
        for (std::size_t i = 0; i < std::min<std::size_t>(length, 256); ++i) png.palette[i][3] = chunk[i];
      } else if ((png.color == 0 && length == 2) || (png.color == 2 && length == 6)) {
        png.keyed = true;
        for (uint32_t i = 0; i < length / 2; ++i) png.key[i] = static_cast<unsigned>(chunk[i * 2]) << 8 | chunk[i * 2 + 1];
      }
    } else if (std::memcmp(type, "IDAT", 4) == 0) {
      if (length) compressed.push_back({chunk, length});
    } else if (std::memcmp(type, "IEND", 4) == 0) {
      break;
    }
  }
  if (!header || compressed.empty() || !png.width || !png.height || (png.color == 3 && !png.paletteSize))
    return DecodeFailure::Format;
  if (png.width > static_cast<uint32_t>(PictureFit::kMaxSourceSide) ||
      png.height > static_cast<uint32_t>(PictureFit::kMaxSourceSide) ||
      static_cast<uint64_t>(png.width) * png.height > kMaxPixels)
    return DecodeFailure::TooManyPixels;
  PictureFit fit;
  if (!fit.begin(static_cast<int>(png.width), static_cast<int>(png.height), width, height))
    return DecodeFailure::TooManyPixels;

  const std::size_t bitsPerPixel = static_cast<std::size_t>(png.channels) * png.depth;
  const std::size_t stride = (png.width * bitsPerPixel + 7) / 8;
  const std::size_t step = std::max<std::size_t>(1, bitsPerPixel / 8);
  std::vector<uint8_t> row(stride + 1), previous(stride + 1, 0);
  std::size_t filled = 0;
  uint32_t y = 0;
  bool broken = false;
  // Stops once the last row is in: whatever the stream holds beyond it is never unpacked.
  inflateZlib(compressed.data(), compressed.size(), [&](const uint8_t* bytes, std::size_t count) {
    while (count && y < png.height) {
      const std::size_t take = std::min(count, row.size() - filled);
      std::memcpy(row.data() + filled, bytes, take);
      filled += take;
      bytes += take;
      count -= take;
      if (filled < row.size()) break;
      if (!unfilter(row.data(), previous.data(), stride, step)) {
        broken = true;
        return false;
      }
      png.emit(row.data() + 1, static_cast<int>(y), fit);
      std::swap(row, previous);
      filled = 0;
      ++y;
    }
    return y < png.height;
  });
  if (broken || y != png.height) return DecodeFailure::Format;
  if (!fit.finish(out.pixels)) return DecodeFailure::Memory;
  out.width = width;
  out.height = height;
  return DecodeFailure::None;
}

}
