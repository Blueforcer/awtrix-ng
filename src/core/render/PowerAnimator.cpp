#include "core/render/PowerAnimator.h"

#include "core/Transitions.h"
#include "core/render/TransitionComposer.h"

namespace awtrix {
namespace render {

namespace {
constexpr int kFallDir = 1;
}

PowerAnimator::PowerAnimator(int width, int height)
    : last_(width, height), scratch_(width, height), black_(width, height) {}

void PowerAnimator::enter(Phase phase, int64_t nowMs, float atProgress) {
  if (atProgress < 0.0f) atProgress = 0.0f;
  if (atProgress > 1.0f) atProgress = 1.0f;
  phase_ = phase;
  p_ = atProgress;
  startMs_ = nowMs - static_cast<int64_t>(atProgress * static_cast<float>(kPowerFadeMs));
}

bool PowerAnimator::advance(int64_t nowMs) {
  const int64_t elapsed = nowMs - startMs_;
  if (elapsed <= 0) {
    p_ = 0.0f;
  } else {
    p_ = static_cast<float>(elapsed) / static_cast<float>(kPowerFadeMs);
    if (p_ > 1.0f) p_ = 1.0f;
  }
  return p_ >= 1.0f;
}

// Flipping the switch mid-fade enters the opposite phase at the mirrored progress, so the picture
// carries on from where it is instead of jumping.
PowerAnimator::Phase PowerAnimator::update(bool on, int64_t nowMs) {
  switch (phase_) {
    case Phase::Live:
      if (!on) enter(Phase::Out, nowMs, 0.0f);
      break;
    case Phase::Out:
      if (on) {
        enter(Phase::In, nowMs, 1.0f - p_);
      } else if (advance(nowMs)) {
        phase_ = Phase::Off;
      }
      break;
    case Phase::Off:
      if (on) enter(Phase::In, nowMs, 0.0f);
      break;
    case Phase::In:
      if (!on) {
        enter(Phase::Out, nowMs, 1.0f - p_);
      } else if (advance(nowMs)) {
        phase_ = Phase::Live;
        p_ = 0.0f;
      }
      break;
  }
  return phase_;
}

void PowerAnimator::syncDims(const Canvas& ref) {
  if (black_.width() == ref.width() && black_.height() == ref.height()) return;
  last_ = Canvas(ref.width(), ref.height());
  scratch_ = Canvas(ref.width(), ref.height());
  black_ = Canvas(ref.width(), ref.height());
}

// Reuses the Rain transition against a black canvas, so the picture drops out of the panel row by
// row rather than simply dimming.
void PowerAnimator::composeOut(Canvas& out) {
  syncDims(out);
  composeTransition(out, last_, black_, Transition::Rain, p_, kFallDir);
}

void PowerAnimator::finish(Canvas& live) {
  syncDims(live);
  if (phase_ == Phase::In) {
    scratch_ = live;
    composeTransition(live, black_, scratch_, Transition::Rain, p_, kFallDir);
  }
  last_ = live;
}

}
}
