#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "media/PodBuffer.h"

namespace awtrix::media {

// A picture fetched from a URL, fitted to the size it was asked for: either still pixels of
// exactly that size, or a GIF no larger than it, kept compressed so it can still play.
struct RemoteImage {
  PodBuffer<uint32_t> pixels;
  PodBuffer<uint8_t> gif;
  int width = 0;
  int height = 0;

  bool copyFrom(const RemoteImage& other) {
    width = height = 0;
    if (!copy(pixels, other.pixels) || !copy(gif, other.gif)) {
      pixels.clear();
      gif.clear();
      return false;
    }
    width = other.width;
    height = other.height;
    return true;
  }
  std::size_t bytes() const { return pixels.size() * sizeof(uint32_t) + gif.size(); }

 private:
  template <typename T>
  static bool copy(PodBuffer<T>& to, const PodBuffer<T>& from) {
    if (from.empty()) {
      to.clear();
      return true;
    }
    if (!to.resize(from.size(), from.size())) return false;
    std::memcpy(to.data(), from.data(), from.size() * sizeof(T));
    return true;
  }
};

enum class RemoteState : uint8_t { kPending, kReady, kFailed };

// The largest GIF a picture source keeps to play; a larger one fails.
inline constexpr std::size_t kMaxRemoteGifBytes = 8 * 1024;

// Pictures from http(s) URLs, fetched and fitted off the render thread. Only the platforms that
// can fetch provide one; everywhere else a URL icon is simply missing.
class IRemoteImages {
 public:
  virtual ~IRemoteImages() = default;
  // Moves on whenever a picture settles or a failed one may be fetched again, so a caller that
  // was told kPending or kFailed knows when asking again can give another answer. Read it before
  // get().
  virtual uint32_t generation() const = 0;
  // Copies a settled picture into out. A picture nobody asked for yet is fetched from here on;
  // kPending until then. kFailed is not final: asked for again after a while, it is fetched again.
  virtual RemoteState get(std::string_view url, int width, int height, RemoteImage& out) = 0;
};

}
