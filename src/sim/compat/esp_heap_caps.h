#pragma once


#include <cstddef>
#include <cstdint>

#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)

// The host has no heap-caps allocator and no PSRAM, so these hand back fixed ESP32-shaped numbers:
// PSRAM always reads as absent, internal RAM as a comfortable but not infinite budget.
inline std::size_t heap_caps_get_free_size(uint32_t caps) {
  return (caps & MALLOC_CAP_SPIRAM) ? 0u : 200u * 1024u;
}
inline std::size_t heap_caps_get_minimum_free_size(uint32_t caps) {
  return (caps & MALLOC_CAP_SPIRAM) ? 0u : 150u * 1024u;
}
inline std::size_t heap_caps_get_total_size(uint32_t caps) {
  return (caps & MALLOC_CAP_SPIRAM) ? 0u : 320u * 1024u;
}
inline std::size_t heap_caps_get_largest_free_block(uint32_t caps) {
  return (caps & MALLOC_CAP_SPIRAM) ? 0u : 120u * 1024u;
}
