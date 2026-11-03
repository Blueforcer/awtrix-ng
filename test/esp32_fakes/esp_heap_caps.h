#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

constexpr uint32_t MALLOC_CAP_INTERNAL = 1u << 11;
constexpr uint32_t MALLOC_CAP_8BIT = 1u << 2;

namespace fake {
inline std::atomic<std::size_t> heapFree{200000};
inline std::atomic<std::size_t> heapLargest{100000};
}

inline std::size_t heap_caps_get_free_size(uint32_t) { return fake::heapFree; }
inline std::size_t heap_caps_get_largest_free_block(uint32_t) { return fake::heapLargest; }
