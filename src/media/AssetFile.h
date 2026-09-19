#pragma once

#include <cstdint>
#include <string>

#include "media/PodBuffer.h"

namespace awtrix {
namespace media {

// Reports allocation failure separately from a missing, empty or unreadable asset so callers
// can retry memory pressure. The optional flag is reset on every call, including success.
bool readAsset(const std::string& path, PodBuffer<uint8_t>& out, bool* outOfMemory = nullptr);

}
}
