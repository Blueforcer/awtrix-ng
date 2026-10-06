#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "core/render/Canvas.h"

namespace awtrix::tc002::voice {
// Rendering only: no capture ownership, network, retained PCM or allocation.
class VoiceOverlay {
 public:
  enum class Phase { Starting, Listening, Processing, Speaking, Error };
  void reset(int64_t now);
  void audio(const int16_t* samples, std::size_t count, int64_t now);
  void draw(Canvas& canvas, Phase phase, int64_t now);

 private:
  std::array<float, 11> levels_{};
  int64_t began_ = 0, audioAt_ = 0, phaseAt_ = 0;
  float envelope_ = 0;
  Phase phase_ = Phase::Starting;
};
}  // namespace awtrix::tc002::voice
