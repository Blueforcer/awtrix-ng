#pragma once

#include <cstdint>
#include <vector>

namespace awtrix {

// Row-major buffer of 0xRRGGBB pixels, origin top-left with +y pointing down. Writes outside the
// canvas are dropped rather than wrapped or clamped.
class Canvas {
 public:
  Canvas(int width, int height);
  // Non-owning view: pixels must hold at least width * height elements and outlive this
  // Canvas and any copies of it. No allocation or initial clearing is performed. A null
  // buffer or a nonpositive dimension creates an empty 0x0 canvas.
  Canvas(int width, int height, uint32_t* pixels);

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

  const uint32_t* data() const { return externalPixels_ ? externalPixels_ : pixels_.data(); }
  uint32_t* data() { return externalPixels_ ? externalPixels_ : pixels_.data(); }
  std::size_t size() const {
    return externalPixels_ ? static_cast<std::size_t>(width_) * height_ : pixels_.size();
  }

 private:
  bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
  bool writable(int x, int y) const { return inBounds(x, y) && x >= clipLeft_ && x <= clipRight_; }
  int width_;
  int height_;
  int clipLeft_ = 0;
  int clipRight_ = 0;
  std::vector<uint32_t> pixels_;
  uint32_t* externalPixels_ = nullptr;
};

}
