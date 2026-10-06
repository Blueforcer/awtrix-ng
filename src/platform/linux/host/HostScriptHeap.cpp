#include "platform/linux/host/HostScriptHeap.h"

#include <cstdlib>

#include "core/script/ScriptHeap.h"

// Host build of the script heap; ScriptHeapEsp32.cpp is the device one. Plain malloc and no
// reserve enforcement; the budget and the growth room come from the platform, so tests and the
// Linux runtime hit the same refusals a device does.
namespace awtrix {
namespace script {
namespace heap {

namespace {

constexpr std::size_t kInternalBudgetBytes = 96 * 1024;
constexpr std::size_t kUnbounded = static_cast<std::size_t>(-1);

const char* g_pool = "internal";
std::size_t g_defaultBudget = kInternalBudgetBytes;
std::size_t g_budget = kInternalBudgetBytes;
std::size_t g_installReserve = 0;
std::function<std::size_t()> g_growth;
std::size_t g_growthOverride = kUnbounded;
bool g_growthOverridden = false;
bool g_reallocFailure = false;

}

void configureHost(const char* poolName, std::size_t budgetBytes,
                   std::function<std::size_t()> growthBudget) {
  g_pool = poolName;
  g_defaultBudget = g_budget = budgetBytes;
  g_growth = std::move(growthBudget);
}

Info info() {
  Info i;
  i.name = g_pool;
  i.budgetBytes = g_budget;
  return i;
}

void setInstallReserve(std::size_t bytes) { g_installReserve = bytes; }
void clearInstallReserve() { g_installReserve = 0; }

std::size_t installLowWater() { return 0; }

std::size_t growthBudget() {
  if (g_growthOverridden) return g_growthOverride;
  return g_growth ? g_growth() : kUnbounded;
}

namespace testing {

void setBudgetBytes(std::size_t bytes) { g_budget = bytes; }
void resetBudgetBytes() { g_budget = g_defaultBudget; }
std::size_t defaultBudgetBytes() { return g_defaultBudget; }

void setGrowthBudget(std::size_t bytes) {
  g_growthOverride = bytes;
  g_growthOverridden = true;
}
void resetGrowthBudget() { g_growthOverridden = false; }
void setReallocFailure(bool fail) { g_reallocFailure = fail; }

}

}
}
}

extern "C" {

void* awtrix_script_heap_alloc(size_t size) { return std::malloc(size); }
void* awtrix_script_heap_realloc(void* ptr, size_t size) {
  if (awtrix::script::heap::g_reallocFailure && size != 0) return nullptr;
  return std::realloc(ptr, size);
}
void awtrix_script_heap_free(void* ptr) { std::free(ptr); }

}
