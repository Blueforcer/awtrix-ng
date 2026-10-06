#pragma once

#include <cstddef>
#include <cstdlib>

namespace awtrix { namespace render {

// Set once by the platform before constructing any frames. The paired release function is
// captured by each owner, so an allocation can always be released by its original allocator.
struct FrameAllocator {
  void* (*allocate)(std::size_t) = std::malloc;
  void (*release)(void*) = std::free;
};
inline FrameAllocator& frameAllocator() {
  static FrameAllocator allocator;
  return allocator;
}
inline void setFrameAllocator(FrameAllocator allocator) { frameAllocator() = allocator; }

} }
