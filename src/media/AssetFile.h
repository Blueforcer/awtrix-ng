#pragma once

#include <cstdint>
#include <string>

#include "media/PodBuffer.h"

namespace awtrix {
namespace media {

bool readAsset(const std::string& path, PodBuffer<uint8_t>& out);

}
}
