#pragma once

#include <cstdint>
#include <memory>

#include "core/render/Canvas.h"

namespace awtrix {
namespace render {

inline constexpr int64_t kPowerFadeMs = 600;

// Fades the panel out and back in around the on/off switch. Call update() every frame: in Out
// phase the frame comes from composeOut(), otherwise render as usual and pass it to finish(). The
// canvas passed to composeOut() must still hold what the last finish() left on it. A fade holds one
// frame of memory while it runs; without that memory it becomes a hard cut.
class PowerAnimator {
 public:
  enum class Phase { Live, Out, Off, In };

  Phase update(bool on, int64_t nowMs);

  Phase phase() const { return phase_; }
  float progress() const { return p_; }
  bool busy() const { return phase_ == Phase::Out || phase_ == Phase::In; }

  void composeOut(Canvas& out);
  // Applies the fade-in if one is running.
  void finish(Canvas& live);

 private:
  void enter(Phase phase, int64_t nowMs, float atProgress);
  bool advance(int64_t nowMs);
  bool hold(const Canvas& frame);
  void release();

  Phase phase_ = Phase::Live;
  int64_t startMs_ = 0;
  float p_ = 0.0f;
  bool shownFrame_ = false;
  bool captureDue_ = false;
  // Fading out: the picture that falls away. Fading in: a copy of the frame being revealed.
  std::unique_ptr<uint32_t[]> held_;
  int heldWidth_ = 0;
  int heldHeight_ = 0;
};

}
}
