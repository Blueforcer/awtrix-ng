#pragma once

#include <cstddef>

namespace awtrix {

// What must stay free so settings and icons still have room once a script's files land.
constexpr std::size_t kFsMarginBytes = 32 * 1024;

// Whether `bytes` can be written without eating into that margin.
inline bool fitsWithMargin(std::size_t freeBytes, std::size_t bytes) {
  return freeBytes > kFsMarginBytes && bytes <= freeBytes - kFsMarginBytes;
}

// How much room the filesystem has left, remembered rather than asked for again. Asking means
// adding up every block in use, which is far more work than a store update is worth: a script
// may update its store on every frame, against a 25 ms frame. The figure is taken once and kept
// until something marks it stale -- a write of our own, or the interval running out, since
// plenty else writes to the same filesystem.
class FsFreeSpace {
 public:
  using ReadFn = std::size_t (*)();

  std::size_t bytes(ReadFn read) {
    if (!known_) {
      free_ = read();
      known_ = true;
    }
    return free_;
  }

  void stale() { known_ = false; }

 private:
  std::size_t free_ = 0;
  bool known_ = false;
};

}
