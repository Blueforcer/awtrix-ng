#include "system/HeapProbe.h"

#ifdef AWTRIX_HEAP_PROBE

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <soc/soc_memory_layout.h>

#include <atomic>
#include <cstdlib>
#include <cstring>

#include "system/HeapCaps.h"
#include "system/Log.h"

// The allocator is intercepted with the linker's --wrap, so every malloc in the firmware, including
// the SDK's, lands in __wrap_malloc below. Only built when AWTRIX_HEAP_PROBE is on.
extern "C" {
void* __real_malloc(size_t size);
void __real_free(void* ptr);
void* __real_calloc(size_t n, size_t size);
void* __real_realloc(void* ptr, size_t size);
}

namespace {

TaskHandle_t g_task = nullptr;
bool g_open = false;

std::size_t g_bytes = 0;
std::size_t g_count = 0;
long g_live = 0;
long g_peak = 0;

// Attribution is restricted to the task that called begin(): the wrappers are global, and without
// this every other task's allocations would be charged to the window under measurement.
inline bool counting() {
  return g_open && xTaskGetCurrentTaskHandle() == g_task;
}

inline std::size_t blockSize(void* p) { return heap_caps_get_allocated_size(p); }

std::atomic<bool> g_watchOpen{false};
TaskHandle_t g_watchTask = nullptr;
std::atomic<std::size_t> g_watchLow{0};
std::atomic<std::size_t> g_watchMax{0};
std::atomic<std::size_t> g_watchBytes{0};
std::atomic<std::size_t> g_watchCount{0};

// Second, coarser channel used by the long-running tasks: the low-water mark is tracked across all
// tasks, while block sizes are only counted for the watching task's own internal-RAM allocations.
inline void watchTake(void* p) {
  if (!g_watchOpen.load(std::memory_order_relaxed)) return;
  const std::size_t free = heap_caps_get_free_size(awtrix::kGuardHeapCaps);
  std::size_t low = g_watchLow.load(std::memory_order_relaxed);
  while (free < low && !g_watchLow.compare_exchange_weak(low, free)) {
  }
  if (xTaskGetCurrentTaskHandle() != g_watchTask) return;
  if (!esp_ptr_internal(p)) return;
  const std::size_t n = blockSize(p);
  std::size_t mx = g_watchMax.load(std::memory_order_relaxed);
  while (n > mx && !g_watchMax.compare_exchange_weak(mx, n)) {
  }
  g_watchBytes.fetch_add(n, std::memory_order_relaxed);
  g_watchCount.fetch_add(1, std::memory_order_relaxed);
}

inline void took(std::size_t n) {
  g_bytes += n;
  ++g_count;
  g_live += static_cast<long>(n);
  if (g_live > g_peak) g_peak = g_live;
}

}

extern "C" void* __wrap_malloc(size_t size) {
  void* p = __real_malloc(size);
  if (p && counting()) took(blockSize(p));
  if (p) watchTake(p);
  return p;
}

extern "C" void* __wrap_calloc(size_t n, size_t size) {
  void* p = __real_calloc(n, size);
  if (p && counting()) took(blockSize(p));
  if (p) watchTake(p);
  return p;
}

extern "C" void __wrap_free(void* ptr) {
  if (ptr && counting()) g_live -= static_cast<long>(blockSize(ptr));
  __real_free(ptr);
}

extern "C" void* __wrap_realloc(void* ptr, size_t size) {
  const std::size_t before = (ptr && counting()) ? blockSize(ptr) : 0;
  void* p = __real_realloc(ptr, size);
  if (counting()) {
    const std::size_t after = p ? blockSize(p) : 0;
    if (after > before) took(after - before);
    else g_live -= static_cast<long>(before - after);
  }
  if (p) watchTake(p);
  return p;
}

namespace awtrix {
namespace probe {

void begin() {
  g_open = false;
  g_task = xTaskGetCurrentTaskHandle();
  g_bytes = 0;
  g_count = 0;
  g_live = 0;
  g_peak = 0;
  g_open = true;
}

namespace {

struct Gate {
  const char* label;
  unsigned long lastMs;
  unsigned long windows;
  unsigned long over;
  unsigned long long bytes;
  unsigned long blocks;
  std::size_t maxBytes;
  long maxPeak;
};
Gate g_gates[24] = {};
bool g_gatesFull = false;

// Labels are compared by pointer, not by content: they are always string literals, and this keeps
// the lookup out of the allocation path it is trying to measure.
Gate* gateFor(const char* label) {
  for (Gate& g : g_gates) {
    if (g.label == label) return &g;
    if (g.label == nullptr) {
      g.label = label;
      g.lastMs = millis();
      return &g;
    }
  }
  if (!g_gatesFull) {
    g_gatesFull = true;
    logf("probe: label table full, some phases are not being reported");
  }
  return nullptr;
}

}

// Closes the window opened by begin() and folds it into the label's bucket. Windows under minBytes
// are counted but not accumulated, and the bucket is only printed once a second.
void report(const char* label, std::size_t minBytes) {
  g_open = false;
  Gate* g = gateFor(label);
  if (!g) return;
  ++g->windows;
  if (g_bytes >= minBytes) {
    ++g->over;
    g->bytes += g_bytes;
    g->blocks += g_count;
    if (g_bytes > g->maxBytes) g->maxBytes = g_bytes;
    if (g_peak > g->maxPeak) g->maxPeak = g_peak;
  }
  const unsigned long now = millis();
  const unsigned long elapsed = now - g->lastMs;
  if (elapsed < 1000) return;
  if (g->over) {
    logf("probe %s: %lu/%lu windows/s, %lu B/s in %lu blocks/s, worst %u B, peak live %ld B",
         g->label ? g->label : "?", g->over, g->windows,
         static_cast<unsigned long>(g->bytes * 1000ULL / elapsed),
         g->blocks * 1000UL / elapsed, static_cast<unsigned>(g->maxBytes), g->maxPeak);
  }
  g->lastMs = now;
  g->windows = 0;
  g->over = 0;
  g->bytes = 0;
  g->blocks = 0;
  g->maxBytes = 0;
  g->maxPeak = 0;
}

void note(const char* label, std::size_t a, std::size_t b, std::size_t c) {
  const bool wasOpen = g_open;
  g_open = false;
  Gate* g = gateFor(label);
  const unsigned long now = millis();
  if (g && now - g->lastMs >= 1000) {
    g->lastMs = now;
    logf("probe note %s: %u / %u / %u", label ? label : "?", static_cast<unsigned>(a),
         static_cast<unsigned>(b), static_cast<unsigned>(c));
  }
  g_open = wasOpen;
}

std::size_t bytesAllocated() { return g_bytes; }
std::size_t allocations() { return g_count; }
long peakLiveBytes() { return g_peak; }

void watchBegin() {
  g_watchOpen.store(false);
  g_watchTask = xTaskGetCurrentTaskHandle();
  g_watchLow.store(heap_caps_get_free_size(kGuardHeapCaps));
  g_watchMax.store(0);
  g_watchBytes.store(0);
  g_watchCount.store(0);
  g_watchOpen.store(true);
}

Watch watchPeek() {
  Watch w;
  w.lowWater = g_watchLow.load();
  w.maxAlloc = g_watchMax.load();
  w.bytes = g_watchBytes.load();
  w.count = g_watchCount.load();
  return w;
}

Watch watchEnd() {
  g_watchOpen.store(false);
  return watchPeek();
}

}
}

#endif
