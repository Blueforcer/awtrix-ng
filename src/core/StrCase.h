#pragma once

#include <cctype>
#include <cstddef>
#include <string>

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

inline bool equalsIgnoreCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (lower(a[i]) != lower(b[i])) return false;
  return true;
}

}
}
