#pragma once

#include <string>

namespace awtrix {
namespace sound {

constexpr size_t kMaxClipName = 32;

// The allowed alphabet has no '/' and no '.', so the returned path cannot
// escape /SOUNDS by construction.
inline std::string clipPathFor(const std::string& name) {
  if (name.empty() || name.size() > kMaxClipName) return "";
  for (char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return "";
  }
  return "/SOUNDS/" + name + ".mp3";
}

}
}
