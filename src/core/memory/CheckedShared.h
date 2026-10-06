#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>

#include "core/memory/CheckedStorage.h"

namespace awtrix::checked {
namespace storage {

// Serves shared_ptr's control block from checked storage.
// Unsupported control-block size or alignment does not compile.
template <class T, std::size_t Capacity> class ReservedAllocator {
 public:
  using value_type = T;
  template <class U> struct rebind { using other = ReservedAllocator<U, Capacity>; };
  ReservedAllocator(void* block, void (*release)(void*)) : block_(block), release_(release) {}
  template <class U> ReservedAllocator(const ReservedAllocator<U, Capacity>& other)
      : block_(other.block_), release_(other.release_) {}
  T* allocate(std::size_t count) {
    static_assert(sizeof(T) <= Capacity, "shared ownership reservation is too small");
    static_assert(alignof(T) <= alignof(std::max_align_t), "unsupported over-aligned owner");
    assert(count == 1);
    (void)count;
    return static_cast<T*>(block_);
  }
  void deallocate(T* block, std::size_t) { release_(block); }
  template <class U> bool operator==(const ReservedAllocator<U, Capacity>& other) const {
    return block_ == other.block_;
  }
  template <class U> bool operator!=(const ReservedAllocator<U, Capacity>& other) const {
    return !(*this == other);
  }
 private:
  template <class, std::size_t> friend class ReservedAllocator;
  void* block_;
  void (*release_)(void*);
};
}

inline constexpr std::size_t kSharedControlBytes = 128;

template <class T> std::shared_ptr<T> tryAdoptShared(std::unique_ptr<T> owner) {
  if (!owner) return {};
  const auto hook = storage::allocator();
  void* block = hook.allocate(kSharedControlBytes);
  if (!block) return {};
  return std::shared_ptr<T>(owner.release(), std::default_delete<T>{},
      storage::ReservedAllocator<T, kSharedControlBytes>(block, hook.release));
}

// T's constructor must itself use checked storage or avoid allocations.
template <class T, class... Args> std::shared_ptr<T> tryMakeShared(Args&&... args) {
  constexpr std::size_t capacity = sizeof(T) + kSharedControlBytes;
  const auto hook = storage::allocator();
  void* block = hook.allocate(capacity);
  if (!block) return {};
  return std::allocate_shared<T>(storage::ReservedAllocator<T, capacity>(block, hook.release),
                                std::forward<Args>(args)...);
}
}
