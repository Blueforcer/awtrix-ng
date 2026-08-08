#pragma once

#include <string>

namespace awtrix {
namespace sound {

constexpr size_t kMaxClipName = 32;
constexpr const char* kDir = "/CLIPS/";
constexpr const char* kExt = ".mp3";

// The allowed alphabet has no '/' and no '.', so the returned path cannot
// escape /CLIPS by construction.
inline std::string clipPathFor(const std::string& name) {
  if (name.empty() || name.size() > kMaxClipName) return "";
  for (char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return "";
  }
  return std::string(kDir) + name + kExt;
}

// The inverse, for state reporting: "/CLIPS/<name>.mp3" back to "<name>".
// Anything shaped differently answers "".
inline std::string clipNameFor(const std::string& path) {
  constexpr size_t kPrefix = sizeof("/CLIPS/") - 1;
  constexpr size_t kSuffix = sizeof(".mp3") - 1;
  if (path.size() <= kPrefix + kSuffix) return "";
  if (path.rfind(kDir, 0) != 0) return "";
  if (path.compare(path.size() - kSuffix, kSuffix, kExt) != 0) return "";
  return path.substr(kPrefix, path.size() - kPrefix - kSuffix);
}

}
}
