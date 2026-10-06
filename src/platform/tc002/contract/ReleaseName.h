#pragma once

#include <cstddef>
#include <string_view>

#include "platform/tc002/contract/release_name.h"

// Release names: the rule of release_name.h for C++.
namespace awtrix {
namespace tc002 {

constexpr std::size_t kMaxReleaseNameBytes = TC002_RELEASE_NAME_LIMIT;

inline bool validReleaseName(std::string_view name) {
  return tc002_release_name_valid(name.data(), name.size());
}

}
}
