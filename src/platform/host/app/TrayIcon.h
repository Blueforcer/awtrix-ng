#pragma once

#include <cstdint>
#include <vector>

#include "platform/host/app/HostSupervisor.h"

namespace awtrix::hostapp {

inline constexpr int kIconLeds = 8;

bool iconLit(int column, int row);
std::vector<uint8_t> iconAlpha(int size);
uint32_t iconColor(HostState state);

}
