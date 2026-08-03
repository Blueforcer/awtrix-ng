#pragma once

#include <string>

namespace awtrix {
namespace version {

struct Parsed {
  long major = 0, minor = 0, patch = 0;
  bool pre = false;
  bool ok = false;
};

inline Parsed parse(const std::string& s) {
  Parsed p;
  std::size_t i = 0;
  if (i < s.size() && (s[i] == 'v' || s[i] == 'V')) ++i;
  long vals[3] = {0, 0, 0};
  int fi = 0;
  for (; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '-') break;
    if (c == '.') { if (++fi > 2) break; continue; }
    if (c < '0' || c > '9') break;
    vals[fi] = vals[fi] * 10 + (c - '0');
    p.ok = true;
  }
  p.major = vals[0];
  p.minor = vals[1];
  p.patch = vals[2];
  p.pre = s.find('-') != std::string::npos;
  return p;
}

// At an equal major.minor.patch a release beats a pre-release, so 1.2.0 counts as newer than
// 1.2.0-rc1. A candidate that does not parse is never newer; an installed one that does not is.
inline bool isNewer(const std::string& installed, const std::string& candidate) {
  const Parsed a = parse(installed);
  const Parsed b = parse(candidate);
  if (!b.ok) return false;
  if (!a.ok) return true;
  if (b.major != a.major) return b.major > a.major;
  if (b.minor != a.minor) return b.minor > a.minor;
  if (b.patch != a.patch) return b.patch > a.patch;
  return a.pre && !b.pre;
}

}
}
