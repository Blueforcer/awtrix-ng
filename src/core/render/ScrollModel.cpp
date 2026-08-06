#include "core/render/ScrollModel.h"

namespace awtrix {
namespace render {

namespace {
constexpr float kBasePxPerFrame = 0.5f;
}

void ScrollModel::reset(const ResolvedScroll& scroll, int64_t nowMs) {
  r_ = scroll;
  cycles_ = 0;
  restart(nowMs);
}

float ScrollModel::startPos() const {
  return r_.entry == ScrollEntry::Offscreen ? r_.xOff : r_.xRest;
}

void ScrollModel::restart(int64_t nowMs) {
  pos_ = startPos();
  lastMs_ = nowMs;
  holdUntilMs_ = r_.entry == ScrollEntry::Offscreen ? nowMs : nowMs + r_.holdMs;
  moving_ = false;
  returning_ = false;
}

void ScrollModel::setStartX(int startX) {
  r_.layout.startX = startX;
  r_.deriveAnchors();
}

void ScrollModel::advance(int64_t nowMs) {
  if (!r_.animates()) {
    lastMs_ = nowMs;
    return;
  }
  if (nowMs <= lastMs_) return;
  lastMs_ = nowMs;
  if (nowMs <= holdUntilMs_) return;
  moving_ = true;

  const float delta = kBasePxPerFrame * (r_.speedPercent / 100.0f);
  const bool toLeft = r_.direction == ScrollDirection::Left;

  if (r_.mode == ScrollMode::Bounce) {
    const float from = toLeft ? r_.xNear : r_.xFar;
    const float to = toLeft ? r_.xFar : r_.xNear;
    if (from == to) return;
    const float target = returning_ ? from : to;
    const float heading = target > pos_ ? 1.0f : -1.0f;
    pos_ += heading * delta;
    if (heading > 0 ? pos_ < target : pos_ > target) return;
    pos_ = target;
    returning_ = !returning_;
    holdUntilMs_ = nowMs + r_.holdMs;
    moving_ = false;
    if (!returning_) ++cycles_;
    return;
  }

  pos_ += toLeft ? -delta : delta;

  // Loop wraps by whole periods rather than restarting, so the tiled copies drawn by
  // drawScrollRun stay lined up and the motion never stutters at the seam.
  if (r_.mode == ScrollMode::Loop) {
    const float ref = startPos();
    const float period = static_cast<float>(r_.period);
    if (period <= 0) return;
    if (toLeft) {
      while (pos_ <= ref - period) {
        pos_ += period;
        ++cycles_;
      }
    } else {
      while (pos_ >= ref + period) {
        pos_ -= period;
        ++cycles_;
      }
    }
    return;
  }

  if (toLeft ? pos_ <= r_.xEnd : pos_ >= r_.xEnd) {
    ++cycles_;
    restart(nowMs);
  }
}

}
}
