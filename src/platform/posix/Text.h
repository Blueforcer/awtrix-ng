#pragma once

#include <cerrno>
#include <cstdlib>
#include <string>
#include <string_view>

namespace awtrix::posix {

inline bool parseInteger(const char* text, int low, int high, int& out) {
  if (!text || !*text) return false;
  char* end = nullptr;
  errno = 0;
  const long value = std::strtol(text, &end, 10);
  if (errno || *end || value < low || value > high) return false;
  out = static_cast<int>(value);
  return true;
}

inline std::string printable(std::string_view text, std::size_t limit = std::string::npos) {
  std::string out(text.substr(0, limit));
  for (char& c : out)
    if (c < 0x20 || static_cast<unsigned char>(c) > 0x7e) c = '?';
  return out;
}

}  // namespace awtrix::posix
