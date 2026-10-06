#pragma once

#include <algorithm>
#include <string_view>

#include "core/render/Canvas.h"
#include "core/render/TextRenderer.h"

namespace awtrix::test {

inline bool lit(uint32_t pixel) { return pixel != 0; }
inline bool red(uint32_t pixel) {
  return (pixel >> 16) > 2 * ((pixel >> 8) & 255) && (pixel >> 16) > 2 * (pixel & 255);
}
inline bool green(uint32_t pixel) {
  return ((pixel >> 8) & 255) > 2 * (pixel >> 16) && ((pixel >> 8) & 255) > 2 * (pixel & 255);
}
inline bool amber(uint32_t pixel) {
  return (pixel >> 16) > 2 * (pixel & 255) && ((pixel >> 8) & 255) > 2 * (pixel & 255);
}
inline bool brightNeutral(uint32_t pixel) {
  const int r = pixel >> 16, g = (pixel >> 8) & 255, b = pixel & 255;
  return std::min({r, g, b}) > 96 && std::max({r, g, b}) - std::min({r, g, b}) < 32;
}

template <typename Predicate>
int countPixels(const Canvas& frame, Predicate matches) {
  int count = 0;
  for (int y = 0; y < frame.height(); ++y)
    for (int x = 0; x < frame.width(); ++x) count += matches(frame.getPixel(x, y));
  return count;
}

inline bool sameFrame(const Canvas& a, const Canvas& b) {
  if (a.width() != b.width() || a.height() != b.height()) return false;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x)
      if (a.getPixel(x, y) != b.getPixel(x, y)) return false;
  return true;
}

inline bool sameShape(const Canvas& a, const Canvas& b) {
  if (a.width() != b.width() || a.height() != b.height()) return false;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x)
      if (lit(a.getPixel(x, y)) != lit(b.getPixel(x, y))) return false;
  return true;
}

struct InkBox {
  int left = 0, top = 0, right = -1, bottom = -1;
  bool found() const { return right >= left && bottom >= top; }
};

template <typename Predicate>
InkBox bounds(const Canvas& frame, Predicate matches) {
  InkBox box{frame.width(), frame.height(), -1, -1};
  for (int y = 0; y < frame.height(); ++y)
    for (int x = 0; x < frame.width(); ++x)
      if (matches(frame.getPixel(x, y))) {
        box.left = std::min(box.left, x);
        box.right = std::max(box.right, x);
        box.top = std::min(box.top, y);
        box.bottom = std::max(box.bottom, y);
      }
  return box;
}

template <typename Predicate>
InkBox findMask(const Canvas& frame, const Canvas& glyphs, Predicate matches) {
  for (int top = 0; top + glyphs.height() <= frame.height(); ++top)
    for (int left = 0; left + glyphs.width() <= frame.width(); ++left) {
      bool same = true;
      for (int y = 0; same && y < glyphs.height(); ++y)
        for (int x = 0; x < glyphs.width(); ++x)
          if (matches(frame.getPixel(left + x, top + y)) != lit(glyphs.getPixel(x, y))) {
            same = false;
            break;
          }
      if (same) return {left, top, left + glyphs.width() - 1, top + glyphs.height() - 1};
    }
  return {};
}

// Finds readable text anywhere on the panel, independent of its position and exact colour.
template <typename Predicate>
InkBox findText(const Canvas& frame, const GfxFont& font, std::string_view label, Predicate matches) {
  const auto metrics = text::measureInk(font, label);
  if (!metrics.hasInk()) return {};
  Canvas glyphs(metrics.inkWidth(), metrics.inkHeight());
  glyphs.clear();
  text::drawText(glyphs, font, -metrics.inkLeft, -metrics.inkTop, label, 1);
  return findMask(frame, glyphs, matches);
}

inline InkBox findText(const Canvas& frame, const GfxFont& font, std::string_view label) {
  return findText(frame, font, label, lit);
}

}
