#include "core/render/Canvas.h"

#include <cstdlib>
#include <cstring>
#include <utility>
#include <algorithm>
#include <limits>

namespace awtrix {

Canvas::Canvas(int width, int height) {
  if (width <= 0 || height <= 0 || static_cast<std::size_t>(width) >
      std::numeric_limits<std::size_t>::max() / sizeof(uint32_t) / height) return;
  const auto allocator = render::frameAllocator();
  pixels_ = static_cast<uint32_t*>(allocator.allocate(
      static_cast<std::size_t>(width) * height * sizeof(uint32_t)));
  if (!pixels_) return;
  release_ = allocator.release;
  width_ = width;
  height_ = height;
  clipRight_ = width_ - 1;
  clipBottom_ = height_ - 1;
  clear();
}

Canvas::Canvas(int width, int height, uint32_t* pixels)
    : width_(pixels && width > 0 && height > 0 ? width : 0),
      height_(width_ > 0 ? height : 0),
      clipRight_(width_ - 1),
      clipBottom_(height_ - 1),
      externalPixels_(width_ > 0 ? pixels : nullptr) {}

Canvas::~Canvas() { releasePixels(); }
void Canvas::releasePixels() {
  if (pixels_ && release_) release_(pixels_);
  pixels_ = nullptr;
  release_ = nullptr;
}
Canvas::Canvas(const Canvas& other) : Canvas(0, 0) { *this = other; }
Canvas& Canvas::operator=(const Canvas& other) {
  if (this == &other) return *this;
  if (other.externalPixels_) {
    releasePixels();
    externalPixels_ = other.externalPixels_;
  } else if (!pixels_ || width_ != other.width_ || height_ != other.height_) {
    Canvas replacement(other.width_, other.height_);
    // A failed copy clears the destination rather than pretending to contain the requested frame.
    *this = std::move(replacement);
    if (other.size() && !valid()) return *this;
  }
  width_ = other.width_;
  height_ = other.height_;
  clipLeft_ = other.clipLeft_;
  clipRight_ = other.clipRight_;
  clipTop_ = other.clipTop_;
  clipBottom_ = other.clipBottom_;
  if (!other.externalPixels_ && other.size()) {
    externalPixels_ = nullptr;
    std::memcpy(pixels_, other.data(), other.size() * sizeof(uint32_t));
  }
  return *this;
}
Canvas::Canvas(Canvas&& other) noexcept : Canvas(0, 0) { *this = std::move(other); }
Canvas& Canvas::operator=(Canvas&& other) noexcept {
  if (this == &other) return *this;
  releasePixels();
  width_ = other.width_; height_ = other.height_;
  clipLeft_ = other.clipLeft_; clipRight_ = other.clipRight_;
  clipTop_ = other.clipTop_; clipBottom_ = other.clipBottom_;
  pixels_ = other.pixels_; release_ = other.release_; externalPixels_ = other.externalPixels_;
  other.width_ = other.height_ = 0;
  other.pixels_ = other.externalPixels_ = nullptr;
  other.release_ = nullptr;
  return *this;
}

void Canvas::setClipX(int left, int right) {
  clipLeft_ = left > 0 ? left : 0;
  clipRight_ = right < width_ - 1 ? right : width_ - 1;
}

void Canvas::setClipRect(int x, int y, int width, int height) {
  clipLeft_ = std::max(x, 0);
  clipTop_ = std::max(y, 0);
  const int64_t right = static_cast<int64_t>(x) + width - 1;
  const int64_t bottom = static_cast<int64_t>(y) + height - 1;
  clipRight_ = width > 0 ? static_cast<int>(std::max<int64_t>(-1, std::min<int64_t>(width_ - 1, right))) : -1;
  clipBottom_ = height > 0 ? static_cast<int>(std::max<int64_t>(-1, std::min<int64_t>(height_ - 1, bottom))) : -1;
}

void Canvas::clear(uint32_t rgb) {
  uint32_t* pixels = data();
  for (std::size_t i = 0; i < size(); ++i) pixels[i] = rgb;
}

// Bresenham with a single error term: err tracks the accumulated deviation so no division or
// floating point is needed.
void Canvas::drawLine(int x0, int y0, int x1, int y1, uint32_t rgb) {
  int dx = std::abs(x1 - x0);
  int dy = -std::abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    setPixel(x0, y0, rgb);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Canvas::drawRect(int x, int y, int w, int h, uint32_t rgb) {
  if (w <= 0 || h <= 0) return;
  drawLine(x, y, x + w - 1, y, rgb);
  drawLine(x, y + h - 1, x + w - 1, y + h - 1, rgb);
  drawLine(x, y, x, y + h - 1, rgb);
  drawLine(x + w - 1, y, x + w - 1, y + h - 1, rgb);
}

void Canvas::fillRect(int x, int y, int w, int h, uint32_t rgb) {
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) setPixel(x + i, y + j, rgb);
}

// Midpoint circle: only one octant is stepped, the other seven are mirrored from it.
void Canvas::drawCircle(int cx, int cy, int r, uint32_t rgb) {
  if (r < 0) return;
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    setPixel(cx + x, cy + y, rgb);
    setPixel(cx + y, cy + x, rgb);
    setPixel(cx - y, cy + x, rgb);
    setPixel(cx - x, cy + y, rgb);
    setPixel(cx - x, cy - y, rgb);
    setPixel(cx - y, cy - x, rgb);
    setPixel(cx + y, cy - x, rgb);
    setPixel(cx + x, cy - y, rgb);
    ++y;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x) + 1;
    }
  }
}

void Canvas::fillCircle(int cx, int cy, int r, uint32_t rgb) {
  if (r < 0) return;
  for (int dy = -r; dy <= r; ++dy)
    for (int dx = -r; dx <= r; ++dx)
      if (dx * dx + dy * dy <= r * r) setPixel(cx + dx, cy + dy, rgb);
}

}
