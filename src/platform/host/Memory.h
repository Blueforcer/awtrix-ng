#pragma once

#include <cstdint>
#include <string_view>

namespace awtrix::os {

bool availableMemory(std::uint64_t& bytes);
bool parseMemAvailable(std::string_view meminfo, std::uint64_t& bytes);

}
