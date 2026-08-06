#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/payload/ScrollSpec.h"
#include "core/render/Canvas.h"
#include "core/render/Font.h"
#include "core/render/ScrollController.h"

namespace awtrix::script {

struct ScrollRun {
  int x = 0;
  int y = 0;
  int width = 0;
  uint32_t color = 0xFFFFFFu;
  const uint32_t* glyphColors = nullptr;
  std::size_t glyphCount = 0;
  int repeat = 0;
  ScrollSpec spec;
};

// Scroll position must survive between frames, but a script re-issues its scroll_text() calls
// from scratch every draw(). This holds that state per app, keyed by the y it was drawn at.
class ScrollBank {
 public:
  // The panel is 8 px tall: two lines of scrolling text is all that can be seen at once.
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
    int y = 0;
    int repeat = 0;
    uint32_t used = 0;
    render::ScrollController scroll;
  };

  Line& lineFor(int y);

  Line lines_[kLines];
  uint32_t clock_ = 0;
};

}
