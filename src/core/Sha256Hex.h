#pragma once

#include <string>

namespace awtrix {

inline bool isSha256Hex(const std::string& value) {
  if (value.size() != 64) return false;
  for (const char c : value)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

}
