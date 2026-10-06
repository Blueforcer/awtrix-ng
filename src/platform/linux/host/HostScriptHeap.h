#pragma once

#include <cstddef>
#include <functional>

namespace awtrix {
namespace script {
namespace heap {

// The host has no fixed heap to measure, so the platform says what the script VM's pool is called
// in the device state, how large the VM may grow and how far a buffer may still grow right now.
// Unconfigured, the host heap behaves like the ESP32 without PSRAM: "internal", 96 KB, unbounded
// growth.
void configureHost(const char* poolName, std::size_t budgetBytes,
                   std::function<std::size_t()> growthBudget);

namespace testing {

void setBudgetBytes(std::size_t bytes);
void resetBudgetBytes();
std::size_t defaultBudgetBytes();

void setGrowthBudget(std::size_t bytes);
void resetGrowthBudget();

// Reject reallocations while leaving existing blocks valid, as a constrained device can.
void setReallocFailure(bool fail);

}
}
}
}
