#pragma once

#include <cstddef>


namespace awtrix {
namespace script {
namespace heap {
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
