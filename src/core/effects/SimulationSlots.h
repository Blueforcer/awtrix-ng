#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace awtrix {
namespace fx {

class SimulationStorage {
 protected:
  struct State {
    int64_t frame;
    float speed;
    uint32_t used;
    int16_t w, h;
    bool live;
  };

  SimulationStorage() = default;
  SimulationStorage(const SimulationStorage&) = delete;
  SimulationStorage& operator=(const SimulationStorage&) = delete;
  ~SimulationStorage();

  // Each slot starts with State. A nonzero seed means the selected simulation needs reset.
  void* select(std::size_t stride, int count, int w, int h, float speed, int64_t frame,
               uint32_t& seed);
  void finish(State& state, int64_t frame) {
    state.frame = frame;
    state.used = ++clock_;
  }

 private:
  void* slots_ = nullptr;
  void (*release_)(void*) = nullptr;
  uint32_t clock_ = 0;
};

// State for the effects that cannot be a pure function of time, such as a ball that clears
// bricks. One effect instance serves every app, transition and script that names it, so each
// canvas size and speed gets its own simulation: two views of the same effect never step each
// other's state. A simulation advances one move per frame step, a long absence is not replayed,
// and the storage is allocated on first use only.
template <typename Sim, int kSlots = 2>
class SimulationSlots : private SimulationStorage {
  static_assert(std::is_trivially_copyable<Sim>::value, "simulations live in raw, zeroed memory");

 public:
  static constexpr int64_t kMaxCatchUp = 16;

  SimulationSlots() = default;
  SimulationSlots(const SimulationSlots&) = delete;
  SimulationSlots& operator=(const SimulationSlots&) = delete;

  // Null only when the first allocation fails; the caller then draws nothing.
  Sim* advance(int w, int h, float speed, int64_t frame) {
    uint32_t seed;
    auto* slot = static_cast<Slot*>(select(sizeof(Slot), kSlots, w, h, speed, frame, seed));
    if (!slot) return nullptr;
    if (seed) slot->sim.reset(w, h, seed);
    const int64_t due = frame - slot->state.frame;
    for (int64_t i = 0; i < due && i < kMaxCatchUp; ++i) slot->sim.step();
    finish(slot->state, frame);
    return &slot->sim;
  }

 private:
  struct Slot {
    State state;
    Sim sim;
  };
  static_assert(std::is_standard_layout<Slot>::value, "slot state must precede the simulation");
  static_assert(kSlots > 0, "at least one simulation slot is required");
};

}
}
