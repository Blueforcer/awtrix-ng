#include "core/script/ScriptHeap.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "system/HeapCaps.h"


namespace awtrix {
namespace script {
namespace heap {

namespace {

// What the script VM may use when it has to live in internal RAM. Deliberately well short of what
// is free, since the network stack and the display need the rest.
constexpr std::size_t kInternalBudgetBytes = 96 * 1024;

// With PSRAM the VM gets half of what is free, leaving the other half for image buffers and
// anything else that gets steered out of internal RAM.
constexpr std::size_t kPsramBudgetNumerator = 1;
constexpr std::size_t kPsramBudgetDenominator = 2;

bool siliconAllowsPsram() {
#if defined(CONFIG_IDF_TARGET_ESP32)
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  unsigned revision = chip.revision;
  if (revision < 100) revision *= 100;
  return !(chip.model == CHIP_ESP32 && revision < 300);
#else
  return true;
#endif
}

struct Decision {
  bool usePsram = false;
  std::size_t budgetBytes = kInternalBudgetBytes;
};

// Decided once at first use and then frozen: the VM's allocations have to keep coming from the same
// pool for its whole life, and free PSRAM shrinks as the device runs.
const Decision& decision() {
  static const Decision d = [] {
    Decision out;
    const std::size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psramFree == 0 || !siliconAllowsPsram()) return out;
    out.usePsram = true;
    out.budgetBytes = psramFree / kPsramBudgetDenominator * kPsramBudgetNumerator;
    return out;
  }();
  return d;
}

}

Info info() {
  const Decision& d = decision();
  Info i;
  i.name = d.usePsram ? "psram" : "internal";
  i.budgetBytes = d.budgetBytes;
  return i;
}

std::size_t g_installReserve = 0;
std::size_t g_installLowWater = 0;

void setInstallReserve(std::size_t bytes) {
  g_installReserve = bytes;
  g_installLowWater = 0;
}
void clearInstallReserve() { g_installReserve = 0; }

std::size_t installLowWater() { return g_installLowWater; }

void noteFree(std::size_t freeNow) {
  if (g_installLowWater == 0 || freeNow < g_installLowWater) g_installLowWater = freeNow;
}

// Only meaningful while a script is being installed, and only without PSRAM: it caps the VM so a
// large script cannot eat the internal RAM the rest of the firmware is still running on.
bool reserveApplies() { return g_installReserve != 0 && !decision().usePsram; }

}
}
}

extern "C" {

void* awtrix_script_heap_alloc(size_t size) {
  using namespace awtrix::script::heap;
  if (reserveApplies()) {
    const std::size_t freeNow = heap_caps_get_free_size(awtrix::kGuardHeapCaps);
    noteFree(freeNow);
    if (!allocFitsReserve(freeNow, size, g_installReserve)) return nullptr;
  }
  return decision().usePsram ? heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                             : malloc(size);
}

void* awtrix_script_heap_realloc(void* ptr, size_t size) {
  using namespace awtrix::script::heap;
  if (reserveApplies()) {
    const std::size_t freeNow = heap_caps_get_free_size(awtrix::kGuardHeapCaps);
    noteFree(freeNow);
    if (!allocFitsReserve(freeNow, size, g_installReserve)) return nullptr;
  }
  return decision().usePsram ? heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                             : realloc(ptr, size);
}

void awtrix_script_heap_free(void* ptr) { free(ptr); }

// Berry asks for 32-bit-only memory for its bytecode. That capability exists in internal RAM, so
// the request is refused outright when the VM lives in PSRAM and Berry falls back to normal memory.
void* berry_malloc32(size_t size) {
  using namespace awtrix::script::heap;
  if (size == 0 || (size & 3) || decision().usePsram) return nullptr;
  return heap_caps_malloc(size, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
}

}
