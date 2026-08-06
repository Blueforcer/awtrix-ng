#pragma once

#include <cstdint>

#include "core/payload/ScrollSpec.h"
#include "core/render/ScrollModel.h"
#include "core/render/ScrollResolver.h"

namespace awtrix {
namespace render {

class ScrollController {
 public:
  void set(const ScrollSpec& spec, const ScrollDefaults& defaults, const ScrollLayout& layout,
           int64_t nowMs);

  void restart(int64_t nowMs) { model_.reset(resolved_, nowMs); }

  void advance(int64_t nowMs, int repeat = 0) { model_.advance(nowMs, repeat); }

  void setStartX(int startX) { model_.setStartX(startX); }

  float x() const { return model_.x(); }
  int cycles() const { return model_.cycles(); }
  const ResolvedScroll& resolved() const { return resolved_; }

  // Text that fits, or a page without a repeat count, never holds up the rotation timer.
  bool countsPasses(int repeat) const { return repeat > 0 && resolved_.animates(); }

  bool wantsMoreTime(int repeat) const {
    return countsPasses(repeat) && model_.cycles() < repeat;
  }

  bool passesDone(int repeat) const {
    return countsPasses(repeat) && model_.cycles() >= repeat;
  }

 private:
  ResolvedScroll resolved_;
  ScrollModel model_;
};

}
}
