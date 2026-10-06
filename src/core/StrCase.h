#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace awtrix {
namespace strcase {

inline char lower(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline std::string toLower(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = lower(c);
  return out;
}

inline char upper(char c) {
  return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

inline std::string toUpper(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = upper(c);
  return out;
}

inline bool equalsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (lower(a[i]) != lower(b[i])) return false;
  return true;
}

// Alphabetical order for lists people read: case is ignored and digit runs compare as numbers,
// so "Radio 2" sorts before "radio 10". Names equal under that rule fall back to byte order.
inline bool alphaLess(std::string_view a, std::string_view b) {
  const auto digit = [](char c) { return c >= '0' && c <= '9'; };
  std::size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (digit(a[i]) && digit(b[j])) {
      while (i < a.size() && a[i] == '0') ++i;
      while (j < b.size() && b[j] == '0') ++j;
      std::size_t ei = i, ej = j;
      while (ei < a.size() && digit(a[ei])) ++ei;
      while (ej < b.size() && digit(b[ej])) ++ej;
      if (ei - i != ej - j) return ei - i < ej - j;
      for (; i < ei; ++i, ++j)
        if (a[i] != b[j]) return a[i] < b[j];
      continue;
    }
    const char x = lower(a[i]), y = lower(b[j]);
    if (x != y) return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
    ++i;
    ++j;
  }
  if (i < a.size() || j < b.size()) return j < b.size();
  return a < b;
}

}
}
