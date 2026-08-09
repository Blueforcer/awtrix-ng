#pragma once

#include <string>

namespace awtrix {
namespace sound {

constexpr size_t kMaxMp3Name = 32;
constexpr const char* kDir = "/MP3/";
constexpr const char* kExt = ".mp3";

// The allowed alphabet has no '/' and no '.', so the returned path cannot
// escape /MP3 by construction.
inline std::string mp3PathFor(const std::string& name) {
  if (name.empty() || name.size() > kMaxMp3Name) return "";
  for (char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return "";
  }
  return std::string(kDir) + name + kExt;
}

// The inverse, for state reporting: "/MP3/<name>.mp3" back to "<name>".
// Anything shaped differently answers "".
inline std::string mp3NameFor(const std::string& path) {
  constexpr size_t kPrefix = sizeof("/MP3/") - 1;
  constexpr size_t kSuffix = sizeof(".mp3") - 1;
  if (path.size() <= kPrefix + kSuffix) return "";
  if (path.rfind(kDir, 0) != 0) return "";
  if (path.compare(path.size() - kSuffix, kSuffix, kExt) != 0) return "";
  return path.substr(kPrefix, path.size() - kPrefix - kSuffix);
}

}
}
