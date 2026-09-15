#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "core/render/Canvas.h"

namespace awtrix {
namespace icon {

inline bool isPng(const uint8_t* data, std::size_t size) {
  static const uint8_t kMagic[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  return size >= 8 && std::memcmp(data, kMagic, sizeof(kMagic)) == 0;
}

bool draw(Canvas& canvas, const std::string& iconId, int x, int y);

}
}
