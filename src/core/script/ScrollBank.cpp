#include "core/script/ScrollBank.h"

#include "core/render/ScrollText.h"
#include "core/render/TextRenderer.h"

namespace awtrix::script {

void ScrollBank::beginFrame() {
  for (Line& line : lines_) line.drawn = false;
}

void ScrollBank::clear() {
  for (Line& line : lines_) line = Line{};
  clock_ = 0;
}

// Finds the slot already scrolling at this y, or takes over the least recently used one. A
// recycled slot is reset, so text that moves to a new y restarts rather than jumping.
ScrollBank::Line& ScrollBank::lineFor(int y) {
  for (Line& line : lines_) {
    if (line.active && line.y == y) {
      line.used = ++clock_;
      return line;
    }
  }

  Line* pick = &lines_[0];
  for (Line& line : lines_) {
    if (!line.active) {
      pick = &line;
      break;
    }
    if (line.used < pick->used) pick = &line;
  }

  *pick = Line{};
  pick->y = y;
  pick->active = true;
  pick->used = ++clock_;
  return *pick;
}

int ScrollBank::draw(Canvas& canvas, const GfxFont& font, const std::string& text,
                     const ScrollRun& run, const ScrollDefaults& defaults, int64_t nowMs) {
  Line& line = lineFor(run.y);

  render::ScrollLayout layout;
  layout.text = text::measure(font, text);
  layout.canvasWidth = canvas.width();
  layout.startX = run.x;
  layout.availWidth = run.width;

  line.scroll.set(run.spec, defaults, layout, nowMs);
  line.scroll.advance(nowMs);
  line.repeat = run.repeat;
  line.drawn = true;

  // Text that fits does not scroll, it centres in the box -- but never so far left that it
  // starts outside it, which is what the second adjustment guards against.
  const render::ResolvedScroll& resolved = line.scroll.resolved();
  float x;
  if (resolved.animates()) {
    x = line.scroll.x();
  } else {
    int fixed = run.x + (run.width - layout.text.inkWidth()) / 2 - layout.text.inkLeft;
    if (fixed + layout.text.inkLeft < run.x) fixed = run.x - layout.text.inkLeft;
    x = static_cast<float>(fixed);
  }

  text::TextPaint paint;
  paint.flat = run.color;
  paint.glyphColors = run.glyphColors;
  paint.glyphCount = run.glyphCount;
  canvas.setClipX(run.x, run.x + run.width - 1);
  render::drawScrollRun(canvas, font, x, run.y, text, layout.text.advance, paint, &resolved);
  canvas.clearClipX();
  return line.scroll.cycles();
}

// Whether the rotation should hold this app a little longer to let a line finish. Only lines
// drawn in the current frame count, so text the script stopped issuing releases the app.
bool ScrollBank::wantsMoreTime() const {
  for (const Line& line : lines_)
    if (line.active && line.drawn && line.scroll.wantsMoreTime(line.repeat)) return true;
  return false;
}

}
