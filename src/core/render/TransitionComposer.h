#pragma once

#include <cstdint>

#include "core/Transitions.h"
#include "core/render/Canvas.h"

namespace awtrix {
namespace render {

// Turns the configured effect id into one to actually play. Transition::Random (0) and anything
// out of range picks from the real effects using seed; index 0 is never returned.
inline Transition resolveTransition(int effect, uint32_t seed) {
  if (effect > 0 && effect < static_cast<int>(kTransitionCount)) return static_cast<Transition>(effect);
  const uint32_t choices = static_cast<uint32_t>(kTransitionCount) - 1u;
  return static_cast<Transition>(1 + static_cast<int>(seed % choices));
}

// Writes the blend of from -> to at progress p (0..1) into out. dir < 0 plays the direction-aware
// effects backwards. All three canvases must have the same dimensions.
void composeTransition(Canvas& out, const Canvas& from, const Canvas& to, Transition effect, float p,
                       int dir);

}
}
