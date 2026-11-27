#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace awtrix::os {

inline std::string trimmed(std::string_view text) {
  const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0'; };
  std::size_t begin = 0, end = text.size();
  while (begin < end && blank(text[begin])) ++begin;
  while (end > begin && blank(text[end - 1])) --end;
  return std::string(text.substr(begin, end - begin));
}

// Lowercase hex of size bytes.
inline std::string hexBytes(const void* data, std::size_t size) {
  constexpr char digits[] = "0123456789abcdef";
  const auto* bytes = static_cast<const unsigned char*>(data);
  std::string out;
  out.reserve(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    out += digits[bytes[i] >> 4];
    out += digits[bytes[i] & 15];
  }
  return out;
}

}
