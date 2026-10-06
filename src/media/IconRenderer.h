#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "core/render/Canvas.h"
#include "media/PodBuffer.h"

namespace awtrix {
namespace icon {

inline bool isPng(const uint8_t* data, std::size_t size) {
  static const uint8_t kMagic[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  return size >= 8 && std::memcmp(data, kMagic, sizeof(kMagic)) == 0;
}

bool draw(Canvas& canvas, std::string_view icon, int x, int y,
          bool* outOfMemory = nullptr);

// Decodes an admitted JPEG of at most maxWidth x maxHeight into pixels.
bool decodeNative(const uint8_t* bytes, std::size_t size, int maxWidth, int maxHeight,
                  media::PodBuffer<uint32_t>& pixels, int& width, int& height,
                  bool* outOfMemory = nullptr);

}
}
