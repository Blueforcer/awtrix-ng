#pragma once

#include <array>
#include <cstdint>

namespace awtrix::render {
// Emitted light per output code, from 0 to 65535, monotonically increasing.
using OutputTable = std::array<uint16_t, 256>;
}
