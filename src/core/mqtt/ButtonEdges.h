#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace awtrix::ha {

// The button topics carry edges, not a state: this keeps every change in the order it happened,
// including a press and its release within one frame, and nothing for a control that did not
// change.
template <int Controls>
class ControlEdges {
 public:
  static constexpr int kControls = Controls;
  static constexpr std::size_t kCapacity = 32;
  using State = std::array<bool, kControls>;
  struct Edge {
    uint8_t control = 0;
    bool down = false;
  };

  // Called with the state after every change. A full buffer drops the new edges; the next
  // connect sends the state again anyway.
  void observe(const State& state) {
    for (int i = 0; i < kControls; ++i) {
      if (state[i] == last_[i]) continue;
      last_[i] = state[i];
      if (count_ < kCapacity) buffer_[(head_ + count_++) % kCapacity] = {static_cast<uint8_t>(i), state[i]};
    }
  }
  // A new broker session starts from the current state of the first `controls` controls.
  void resync(const State& state, int controls = kControls) {
    head_ = count_ = 0;
    last_ = state;
    for (int i = 0; i < controls && i < kControls; ++i) buffer_[count_++] = {static_cast<uint8_t>(i), state[i]};
  }
  bool pop(Edge& out) {
    if (!count_) return false;
    out = buffer_[head_];
    head_ = (head_ + 1) % kCapacity;
    --count_;
    return true;
  }

 private:
  State last_{};
  std::array<Edge, kCapacity> buffer_{};
  std::size_t head_ = 0, count_ = 0;
};

using ButtonEdges = ControlEdges<3>;

}
