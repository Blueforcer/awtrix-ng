#include "platform/rp2040/ScriptHeapRp2040.h"

#include <Arduino.h>
#include <malloc.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>

#include "core/script/ScriptHeap.h"
#include "core/script/ScriptServices.h"
#include "platform/BuildFeatures.h"

// What the script VM may grow to. The ESP32 gets 96 KB of internal RAM. The Pico W (RP2040, 264 KB)
// has one heap of about 110 KB shared with Wi-Fi, lwIP, HTTP, MQTT and the frame buffers, so it
// gets less; the Pico 2 W (RP2350, 520 KB) has room for the ESP32's 96 KB. Set from build_flags
// (-D AWTRIX_RP2040_SCRIPT_HEAP_KB=n) to tune a board.
#ifndef AWTRIX_RP2040_SCRIPT_HEAP_KB
#if defined(PICO_RP2350) && PICO_RP2350
#define AWTRIX_RP2040_SCRIPT_HEAP_KB 96
#else
#define AWTRIX_RP2040_SCRIPT_HEAP_KB 48
#endif
#endif

namespace awtrix {
namespace script {
namespace heap {

namespace {

constexpr std::size_t kBudgetBytes = std::size_t(AWTRIX_RP2040_SCRIPT_HEAP_KB) * 1024;

std::size_t g_installReserve = 0;
std::size_t g_installLowWater = 0;

void noteFree(std::size_t freeNow) {
  if (g_installLowWater == 0 || freeNow < g_installLowWater) g_installLowWater = freeNow;
}

// Same rule as the ESP32 seam: while a script installs, the VM may not take the heap below the
// reserve, so an oversized script fails inside the compiler instead of starving Wi-Fi.
bool refusedByReserve(std::size_t size) {
  if (g_installReserve == 0) return false;
  const std::size_t freeNow = rp2040.getFreeHeap();
  noteFree(freeNow);
  return !allocFitsReserve(freeNow, size, g_installReserve);
}

}

// A build without scripting (-D AWTRIX_FEATURE_SCRIPTING=0) has no VM, so it reports the pool as
// "unavailable" with no budget, as documented for /api/v1/device.
Info info() {
  if (!AWTRIX_FEATURE_SCRIPTING) return {"unavailable", 0, false};
  Info i;
  i.name = "internal";
  i.budgetBytes = kBudgetBytes;
  return i;
}

void setInstallReserve(std::size_t bytes) {
  g_installReserve = bytes;
  g_installLowWater = 0;
}
void clearInstallReserve() { g_installReserve = 0; }

std::size_t installLowWater() { return g_installLowWater; }

std::size_t growthBudget() {
  if (!AWTRIX_FEATURE_SCRIPTING) return 0;
  const std::size_t freeNow = rp2040.getFreeHeap();
  const std::size_t largest = picoLargestFreeBlock();
  const std::size_t usable = std::min(freeNow, largest);
  return usable > kInstallHeadroomBytes ? usable - kInstallHeadroomBytes : 0;
}

// newlib cannot walk its free list, so this is the block at the top of the heap: the free chunk
// malloc keeps there plus what sbrk has not handed out yet. The same reading /api/v1/device
// reports as largestFreeBlockBytes.
std::size_t picoLargestFreeBlock() {
  const ptrdiff_t unclaimed = &__StackLimit - static_cast<char*>(sbrk(0));
  return static_cast<std::size_t>(mallinfo().keepcost) +
         static_cast<std::size_t>(std::max<ptrdiff_t>(unclaimed, 0));
}

}
}
}

extern "C" {

void* awtrix_script_heap_alloc(size_t size) {
  if (awtrix::script::heap::refusedByReserve(size)) return nullptr;
  return std::malloc(size);
}

void* awtrix_script_heap_realloc(void* ptr, size_t size) {
  if (awtrix::script::heap::refusedByReserve(size)) return nullptr;
  return std::realloc(ptr, size);
}

void awtrix_script_heap_free(void* ptr) { std::free(ptr); }

}
