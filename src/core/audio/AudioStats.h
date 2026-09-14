#pragma once

#include <cstdint>
#include <type_traits>

namespace awtrix {
namespace audio {

constexpr int kBandCount = 32;

// One analysed frame of the music being played: bass first, 0..255 under automatic gain.
struct FrameStats {
  uint8_t bands[kBandCount] = {};
  uint8_t level = 0;
  bool beat = false;
};
static_assert(std::is_trivially_copyable<FrameStats>::value, "copied inside a seqlock");

}
}
