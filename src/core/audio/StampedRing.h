#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace awtrix::audio {

// One writer, any number of readers. Atomic words carry a generation-stamped
// snapshot; an overlapping copy is race-free and fails validation.
template <typename T, std::size_t Slots>
class StampedRing {
  static_assert(Slots >= 2, "a ring needs at least two slots");
  static_assert((Slots & (Slots - 1)) == 0, "slot mapping must survive generation rollover");
  static_assert(std::is_trivially_copyable<T>::value, "ring values must be trivially copyable");
  static_assert(std::atomic<uint32_t>::is_always_lock_free, "the ring must never take a lock");
  static constexpr std::size_t kWords = 2 + (sizeof(T) + sizeof(uint32_t) - 1) / sizeof(uint32_t);

 public:
  StampedRing() {
    for (std::size_t i = 0; i < Slots; ++i)
      slots_[i].generation.store(static_cast<uint32_t>(i) ^ 1U, std::memory_order_relaxed);
  }

  uint32_t head() const { return head_.load(std::memory_order_acquire); }

  void publish(const T& value, int64_t audibleAtMs) {
    const uint32_t id = head_.load(std::memory_order_relaxed) + 1;
    Slot& slot = slots_[id % Slots];
    uint32_t words[kWords];
    words[kWords - 1] = 0;
    std::memcpy(words, &audibleAtMs, sizeof audibleAtMs);
    std::memcpy(words + 2, &value, sizeof value);
    // Flipping the low bit marks a generation belonging to a different slot.
    slot.generation.store(id ^ 1U, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    for (std::size_t i = 0; i < kWords; ++i) slot.words[i].store(words[i], std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    slot.generation.store(id, std::memory_order_relaxed);
    head_.store(id, std::memory_order_release);
  }

  // Outputs are changed only when the requested generation was copied consistently.
  bool read(uint32_t id, int64_t& audibleAtMs, T& value) const {
    const Slot& slot = slots_[id % Slots];
    if (slot.generation.load(std::memory_order_relaxed) != id) return false;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    uint32_t words[kWords];
    for (std::size_t i = 0; i < kWords; ++i) words[i] = slot.words[i].load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (slot.generation.load(std::memory_order_relaxed) != id) return false;
    std::memcpy(&audibleAtMs, words, sizeof audibleAtMs);
    std::memcpy(static_cast<void*>(&value), words + 2, sizeof value);
    return true;
  }

 private:
  struct Slot {
    std::atomic<uint32_t> generation{0};
    std::atomic<uint32_t> words[kWords]{};
  };
  Slot slots_[Slots];
  std::atomic<uint32_t> head_{0};
};

}  // namespace awtrix::audio
