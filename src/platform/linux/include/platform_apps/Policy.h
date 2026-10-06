#pragma once

#include <algorithm>
#include "core/apps/BuiltinNames.h"

namespace awtrix {
inline bool retiredBuiltin(const std::string& name, const std::vector<std::string>& current) {
  return std::find(current.begin(), current.end(), name) == current.end() &&
         std::find(std::begin(kBuiltinNames), std::end(kBuiltinNames), name) != std::end(kBuiltinNames);
}
}
