#include "core/effects/SimulationSlots.h"

#include <cstring>

#include "core/effects/EffectNoise.h"
#include "core/render/FrameMemory.h"

namespace awtrix::fx {

SimulationStorage::~SimulationStorage() {
  if (slots_) release_(slots_);
}

void* SimulationStorage::select(std::size_t stride, int count, int w, int h, float speed,
                                int64_t frame, uint32_t& seed) {
  if (!slots_) {
    const auto allocator = render::frameAllocator();
    slots_ = allocator.allocate(stride * count);
    if (!slots_) return nullptr;
    std::memset(slots_, 0, stride * count);
    release_ = allocator.release;
  }
  auto* slot = static_cast<State*>(slots_);
  for (int i = 0; i < count; ++i) {
    auto* candidate = reinterpret_cast<State*>(static_cast<char*>(slots_) + i * stride);
    if (candidate->live && candidate->w == w && candidate->h == h && candidate->speed == speed) {
      slot = candidate;
      break;
    }
    if (!candidate->live || candidate->used < slot->used) slot = candidate;
  }
  seed = 0;
  if (!slot->live || slot->w != w || slot->h != h || slot->speed != speed) {
    slot->live = true;
    slot->w = w;
    slot->h = h;
    slot->speed = speed;
    slot->frame = frame;
    seed = noise::hash2(static_cast<uint32_t>(frame), ++clock_) | 1u;
  }
  return slot;
}

}
