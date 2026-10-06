#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/payload/ScrollSpec.h"
#include "core/render/Canvas.h"
#include "core/render/Font.h"
#include "core/render/ScrollController.h"
#include "core/render/TextRenderer.h"

namespace awtrix::script {

struct ScrollRun {
  int x = 0;
  int y = 0;
  int width = 0;
  uint32_t color = 0xFFFFFFu;
  const text::TextRun* runs = nullptr;
  std::size_t runCount = 0;
  int repeat = 0;
  ScrollSpec spec;
};

// Scroll position must survive between frames, but a script re-issues its scroll_text() calls
// from scratch every draw(). This holds that state per app, keyed by the strip it was drawn in:
// its x, y and width, so two strips on one row move independently.
class ScrollBank {
 public:
  // Every app carries its own slots, so the count stays small for the ESP32's RAM.
  static constexpr int kLines = 2;

  void beginFrame();
  int draw(Canvas& canvas, const GfxFont& font, const std::string& text, const ScrollRun& run,
           const ScrollDefaults& defaults, int64_t nowMs);
  bool wantsMoreTime() const;
  void clear();

 private:
  struct Line {
    bool active = false;
    bool drawn = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int repeat = 0;
    uint32_t used = 0;
    render::ScrollController scroll;
  };

  Line& lineFor(int x, int y, int width);

  Line lines_[kLines];
  uint32_t clock_ = 0;
};

}
