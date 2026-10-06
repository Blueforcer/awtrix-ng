#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "media/PodBuffer.h"

namespace awtrix {
namespace media {

// Reports allocation failure separately from a missing, empty or unreadable asset so callers
// can retry memory pressure. The optional flag is reset on every call, including success.
bool readAsset(std::string_view path, PodBuffer<uint8_t>& out, bool* outOfMemory = nullptr);

// Metadata probes read bounded slices without allocating the compressed image.
bool readAssetRange(std::string_view path, std::size_t offset, uint8_t* out,
                    std::size_t capacity, std::size_t& read, std::size_t& fileSize);

}
}
