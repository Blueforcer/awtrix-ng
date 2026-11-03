#include "core/render/PowerAnimator.h"

#include <cstring>
#include <new>

#include "core/Transitions.h"
#include "core/render/TransitionComposer.h"

namespace awtrix {
namespace render {

namespace {
constexpr int kFallDir = 1;
}

void PowerAnimator::enter(Phase phase, int64_t nowMs, float atProgress) {
  if (atProgress < 0.0f) atProgress = 0.0f;
  if (atProgress > 1.0f) atProgress = 1.0f;
  phase_ = phase;
  captureDue_ = phase == Phase::Out;
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
        release();
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
        release();
      }
      break;
  }
  return phase_;
}

// Plain heap rather than the frame allocator: its reserve is there for frames that stay, and would
// turn this fade into a hard cut on any clock with less than 64 KB free.
bool PowerAnimator::hold(const Canvas& frame) {
  if (!held_ || heldWidth_ != frame.width() || heldHeight_ != frame.height()) {
    held_.reset(new (std::nothrow) uint32_t[frame.size()]);
    if (!held_) return false;
    heldWidth_ = frame.width();
    heldHeight_ = frame.height();
  }
  std::memcpy(held_.get(), frame.data(), frame.size() * sizeof(uint32_t));
  return true;
}

void PowerAnimator::release() { held_.reset(); }

// Reuses the Rain transition against a black canvas, so the picture drops out of the panel row by
// row rather than simply dimming. Before the first finish() the canvas holds no frame of ours, only
// whatever the boot screen left there, so a clock restored to "off" goes dark without playing that.
void PowerAnimator::composeOut(Canvas& out) {
  if (captureDue_) {
    captureDue_ = false;
    if (!shownFrame_ || !hold(out)) {
      phase_ = Phase::Off;
      release();
      out.clear(0x000000u);
      return;
    }
  }
  const Canvas black(0, 0, nullptr);
  const Canvas from(heldWidth_, heldHeight_, held_.get());
  composeTransition(out, from, black, Transition::Rain, p_, kFallDir);
}

void PowerAnimator::finish(Canvas& live) {
  shownFrame_ = true;
  if (phase_ != Phase::In) return;
  if (!hold(live)) {
    phase_ = Phase::Live;
    p_ = 0.0f;
    return;
  }
  const Canvas black(0, 0, nullptr);
  const Canvas to(heldWidth_, heldHeight_, held_.get());
  composeTransition(live, black, to, Transition::Rain, p_, kFallDir);
}

}
}
