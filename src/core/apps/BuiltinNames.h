#pragma once

#include <iterator>
#include <string>
#include <vector>

namespace awtrix {
inline constexpr const char* kBuiltinNames[] = {"Time", "Date", "Temperature", "Humidity", "Battery"};
inline std::vector<std::string> defaultBuiltinNames() {
  return {std::begin(kBuiltinNames), std::end(kBuiltinNames)};
}
}
