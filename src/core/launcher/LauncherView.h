#pragma once

#include <cstdint>

#include "core/launcher/Launcher.h"

namespace awtrix {

class Canvas;
struct GfxFont;

namespace launcher {

// Draws the launcher over the finished frame in the manner of the quick settings: one entry per
// 8 px row, the selected one bright and the others dimmed, a small glyph before the label where the
// panel is wide enough. As many rows show as the panel is tall; with a single row the entries slide
// in sideways and a slider on the bottom row shows the position.
class LauncherView {
 public:
  static constexpr long kOpenMs = 180;
  static constexpr long kMoveMs = 160;
  static constexpr long kScrollPauseMs = 900;

  // False once the menu is closed and its closing animation is over: nothing was drawn.
  bool draw(Canvas& canvas, const Launcher& menu, const GfxFont& font, int64_t nowMs);

 private:
  void drawRows(Canvas& canvas, const Launcher& menu, const GfxFont& font, int top, int rows,
                int64_t nowMs);
  void drawSingle(Canvas& canvas, const Launcher& menu, const GfxFont& font, int top,
                  int64_t nowMs);
  void drawEntry(Canvas& canvas, const Entry& entry, const GfxFont& font, int x, int y, int width,
                 float light, bool scroll, int64_t sinceMs, int64_t nowMs);
  void drawSlider(Canvas& canvas, const Launcher& menu, int y);
};

}

}
