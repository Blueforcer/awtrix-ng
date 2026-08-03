#pragma once

#include <cstdint>
#include <string>

#include "core/StrCase.h"

namespace awtrix {

inline constexpr int kMatrixHeight = 8;
inline constexpr int kMatrixWidthMin = 32;
inline constexpr int kMatrixWidthMax = 128;

enum class PanelStart : uint8_t { TopLeft = 0, TopRight, BottomLeft, BottomRight };
enum class Wiring : uint8_t { Rows = 0, Columns };

inline constexpr const char* kPanelStartNames[] = {"topLeft", "topRight", "bottomLeft",
                                                   "bottomRight"};
inline constexpr int kPanelStartCount = 4;
inline constexpr const char* kWiringNames[] = {"rows", "columns"};
inline constexpr int kWiringCount = 2;

inline int enumIndexByName(const char* const* names, int count, const std::string& name) {
  for (int i = 0; i < count; ++i)
    if (strcase::equalsIgnoreCase(names[i], name)) return i;
  return -1;
}

inline std::string enumNameChoices(const char* const* names, int count) {
  std::string msg = "must be one of:";
  for (int i = 0; i < count; ++i) {
    msg += ' ';
    msg += names[i];
  }
  return msg;
}

struct MatrixLayout {
  int panelWidth = 32;
  int panels = 1;
  PanelStart panelStart = PanelStart::TopLeft;
  Wiring panelWiring = Wiring::Rows;
  bool panelSerpentine = true;
  bool panelChainReverse = false;
  bool panelChainSerpentine = false;
  bool mirror = false;
  bool rotate180 = false;

  int width() const { return panelWidth * panels; }
  int height() const { return kMatrixHeight; }
  int ledCount() const { return width() * height(); }

  bool bottomStart() const {
    return panelStart == PanelStart::BottomLeft || panelStart == PanelStart::BottomRight;
  }
  bool rightStart() const {
    return panelStart == PanelStart::TopRight || panelStart == PanelStart::BottomRight;
  }
  // Order the panels sit on the cable. A right-hand start already walks the chain backwards, so
  // the flag XORs against it: the stock layouts keep the index they have always had.
  bool chainReversed() const { return rightStart() != panelChainReverse; }

  // Maps a canvas pixel (origin top-left) to its position in the LED strip, following how the
  // panels are actually wired and chained.
  int xyToIndex(int x, int y) const;
};

MatrixLayout sanitizeMatrixLayout(MatrixLayout in, bool* changed = nullptr);

}
