#pragma once

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include "core/render/FrameMemory.h"

namespace awtrix {

// Row-major buffer of 0xRRGGBB pixels, origin top-left with +y pointing down. Writes outside the
// canvas are dropped rather than wrapped or clamped.
class Canvas {
 public:
  struct ClipRect { int left, top, right, bottom; };
  Canvas(int width, int height);
  // Non-owning view: pixels must hold at least width * height elements and outlive this
  // Canvas and any copies of it. No allocation or initial clearing is performed. A null
  // buffer or a nonpositive dimension creates an empty 0x0 canvas.
  Canvas(int width, int height, uint32_t* pixels);
  ~Canvas();
  Canvas(const Canvas& other);
  Canvas& operator=(const Canvas& other);
  Canvas(Canvas&& other) noexcept;
  Canvas& operator=(Canvas&& other) noexcept;
  // Failed owning allocations produce a safe empty canvas. Copies of external views still borrow.
  bool valid() const { return width_ > 0 && height_ > 0 && data(); }

  int width() const { return width_; }
  int height() const { return height_; }

  void clear(uint32_t rgb = 0x000000u);
  void setPixel(int x, int y, uint32_t rgb) {
    if (writable(x, y)) data()[static_cast<std::size_t>(y) * width_ + x] = rgb & 0xFFFFFFu;
  }
  uint32_t getPixel(int x, int y) const {
    return inBounds(x, y) ? data()[static_cast<std::size_t>(y) * width_ + x] : 0u;
  }

  void drawLine(int x0, int y0, int x1, int y1, uint32_t rgb);
  void drawRect(int x, int y, int w, int h, uint32_t rgb);
  void fillRect(int x, int y, int w, int h, uint32_t rgb);
  void drawCircle(int cx, int cy, int r, uint32_t rgb);
  void fillCircle(int cx, int cy, int r, uint32_t rgb);

  // Column mask for drawing, inclusive on both ends. Only setPixel honours it; clear() and
  // getPixel() ignore it.
  void setClipX(int left, int right);
  void clearClipX() { setClipX(0, width_ - 1); }
  // Rectangular clip for pixel writes; clear() ignores it.
  // Nested drawing helpers save and restore the previous clip.
  ClipRect clipRect() const { return {clipLeft_, clipTop_, clipRight_, clipBottom_}; }
  void setClipRect(int x, int y, int width, int height);
  void restoreClipRect(ClipRect clip) {
    clipLeft_ = clip.left; clipTop_ = clip.top;
    clipRight_ = clip.right; clipBottom_ = clip.bottom;
  }

  const uint32_t* data() const { return externalPixels_ ? externalPixels_ : pixels_; }
  uint32_t* data() { return externalPixels_ ? externalPixels_ : pixels_; }
  std::size_t size() const {
    return static_cast<std::size_t>(width_) * height_;
  }

 private:
  bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
  bool writable(int x, int y) const {
    return inBounds(x, y) && x >= clipLeft_ && x <= clipRight_ &&
           y >= clipTop_ && y <= clipBottom_;
  }
  void releasePixels();
  int width_ = 0;
  int height_ = 0;
  int clipLeft_ = 0;
  int clipRight_ = 0;
  int clipTop_ = 0;
  int clipBottom_ = -1;
  uint32_t* pixels_ = nullptr;
  void (*release_)(void*) = nullptr;
  uint32_t* externalPixels_ = nullptr;
};

// Narrows the clip to a rectangle while it lives, never widening what the caller allowed.
class ClipScope {
 public:
  ClipScope(Canvas& canvas, int x, int y, int width, int height)
      : canvas_(canvas), saved_(canvas.clipRect()) {
    const int left = std::max(saved_.left, x);
    const int top = std::max(saved_.top, y);
    const int right = std::min(saved_.right, x + width - 1);
    const int bottom = std::min(saved_.bottom, y + height - 1);
    canvas.setClipRect(left, top, right - left + 1, bottom - top + 1);
  }
  ~ClipScope() { canvas_.restoreClipRect(saved_); }
  ClipScope(const ClipScope&) = delete;
  ClipScope& operator=(const ClipScope&) = delete;

 private:
  Canvas& canvas_;
  Canvas::ClipRect saved_;
};


}
