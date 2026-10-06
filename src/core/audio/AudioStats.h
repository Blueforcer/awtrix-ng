#pragma once

#include <cstdint>
#include <type_traits>

namespace awtrix {
namespace audio {

constexpr int kBandCount = 32;

// One analysed audio frame: bass first, 0..255 under the source's scaling policy.
struct FrameStats {
  uint8_t bands[kBandCount] = {};
  uint8_t level = 0;
  bool beat = false;
};
static_assert(std::is_trivially_copyable<FrameStats>::value, "copied inside a seqlock");

}
}
