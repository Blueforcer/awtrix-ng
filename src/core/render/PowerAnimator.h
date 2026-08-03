#pragma once

#include <cstdint>

#include "core/render/Canvas.h"

namespace awtrix {
namespace render {

inline constexpr int64_t kPowerFadeMs = 600;

// Fades the panel out and back in around the on/off switch. Call update() every frame: in Out
// phase the frame comes from composeOut(), otherwise render as usual and pass it to finish().
class PowerAnimator {
 public:
  enum class Phase { Live, Out, Off, In };

  PowerAnimator(int width, int height);

  Phase update(bool on, int64_t nowMs);

  Phase phase() const { return phase_; }
  float progress() const { return p_; }
  bool busy() const { return phase_ == Phase::Out || phase_ == Phase::In; }

  void composeOut(Canvas& out);
  // Applies the fade-in if one is running and keeps this frame as the one to fade out from.
  void finish(Canvas& live);

 private:
  void enter(Phase phase, int64_t nowMs, float atProgress);
  bool advance(int64_t nowMs);
  void syncDims(const Canvas& ref);

  Phase phase_ = Phase::Live;
  int64_t startMs_ = 0;
  float p_ = 0.0f;
  Canvas last_;
  Canvas scratch_;
  Canvas black_;
};

}
}
