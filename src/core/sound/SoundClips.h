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

// The inverse, for state reporting: "/SOUNDS/<name>.mp3" back to "<name>".
// Anything shaped differently answers "".
inline std::string clipNameFor(const std::string& path) {
  constexpr size_t kPrefix = 8;  // "/SOUNDS/"
  constexpr size_t kSuffix = 4;  // ".mp3"
  if (path.size() <= kPrefix + kSuffix) return "";
  if (path.rfind("/SOUNDS/", 0) != 0) return "";
  if (path.compare(path.size() - kSuffix, kSuffix, ".mp3") != 0) return "";
  return path.substr(kPrefix, path.size() - kPrefix - kSuffix);
}

}
}
