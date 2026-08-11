#include "core/script/ScriptHeap.h"

#include <cstdlib>

#include "core/script/ScriptHeapTesting.h"


// Host and simulator build of the script heap; ScriptHeapEsp32.cpp is the device one. Plain
// malloc and no reserve enforcement, but the same budget, so tests hit the same refusals.
namespace awtrix {
namespace script {
namespace heap {

namespace {

constexpr std::size_t kInternalBudgetBytes = 96 * 1024;

std::size_t g_budget = kInternalBudgetBytes;
std::size_t g_installReserve = 0;

constexpr std::size_t kUnbounded = static_cast<std::size_t>(-1);
std::size_t g_growthBudget = kUnbounded;

}

Info info() {
  Info i;
  i.name = "internal";
  i.budgetBytes = g_budget;
  return i;
}

void setInstallReserve(std::size_t bytes) { g_installReserve = bytes; }
void clearInstallReserve() { g_installReserve = 0; }

std::size_t installLowWater() { return 0; }

// No real ceiling on the host: growthBudget() only tracks whatever the test hook sets, so tests
// can exercise the same refusals a device build hits without simulating its heap.
std::size_t growthBudget() { return g_growthBudget; }

namespace testing {

void setBudgetBytes(std::size_t bytes) { g_budget = bytes; }
void resetBudgetBytes() { g_budget = kInternalBudgetBytes; }
std::size_t defaultBudgetBytes() { return kInternalBudgetBytes; }

void setGrowthBudget(std::size_t bytes) { g_growthBudget = bytes; }
void resetGrowthBudget() { g_growthBudget = kUnbounded; }

}

}
}
}

extern "C" {

void* awtrix_script_heap_alloc(size_t size) { return std::malloc(size); }
void* awtrix_script_heap_realloc(void* ptr, size_t size) { return std::realloc(ptr, size); }
void awtrix_script_heap_free(void* ptr) { std::free(ptr); }

}
