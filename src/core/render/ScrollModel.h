#pragma once

#include <cstdint>

#include "core/render/ScrollResolver.h"

namespace awtrix {
namespace render {

class ScrollModel {
 public:
  void reset(const ResolvedScroll& scroll, int64_t nowMs);
  void advance(int64_t nowMs, int repeat = 0);

  float x() const { return pos_; }
  // Completed passes: one wrap or loop repeat, or one full there-and-back for bounce.
  int cycles() const { return cycles_; }
  bool moving() const { return moving_; }

  void setStartX(int startX);

 private:
  void restart(int64_t nowMs);
  float startPos() const;
  bool finished(int repeat) const { return repeat > 0 && cycles_ >= repeat; }

  ResolvedScroll r_;
  float pos_ = 0;
  int64_t lastMs_ = 0;
  int64_t holdUntilMs_ = 0;
  int cycles_ = 0;
  bool moving_ = false;
  bool returning_ = false;
};

}
}
