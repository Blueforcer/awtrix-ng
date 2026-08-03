#include "core/render/Canvas.h"

#include <cstdlib>

namespace awtrix {

Canvas::Canvas(int width, int height)
    : width_(width > 0 ? width : 0),
      height_(height > 0 ? height : 0),
      clipRight_(width_ - 1),
      pixels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0u) {}

void Canvas::setClipX(int left, int right) {
  clipLeft_ = left > 0 ? left : 0;
  clipRight_ = right < width_ - 1 ? right : width_ - 1;
}

void Canvas::clear(uint32_t rgb) {
  for (auto& p : pixels_) p = rgb;
}

void Canvas::setPixel(int x, int y, uint32_t rgb) {
  if (!writable(x, y)) return;
  pixels_[static_cast<std::size_t>(y) * width_ + x] = rgb & 0xFFFFFFu;
}

uint32_t Canvas::getPixel(int x, int y) const {
  if (!inBounds(x, y)) return 0u;
  return pixels_[static_cast<std::size_t>(y) * width_ + x];
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
