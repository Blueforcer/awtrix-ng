#pragma once

#include <cctype>
#include <cstddef>
#include <cstdlib>

namespace awtrix {
namespace requesthead {

// Offset just past the blank line that ends an HTTP request head in buf[0, len), or 0 while it has
// not arrived. Scanning starts at `from`, where the previous call stopped, and still finds a blank
// line that the two reads split.
inline std::size_t end(const char* buf, std::size_t len, std::size_t from = 0) {
  for (std::size_t i = from < 3 ? 3 : from; i < len; ++i)
    if (buf[i - 3] == '\r' && buf[i - 2] == '\n' && buf[i - 1] == '\r' && buf[i] == '\n')
      return i + 1;
  return 0;
}

// The Content-Length a head announces, or 0 without one. The name matches in any case, and only
// at the start of a header line.
inline long contentLength(const char* head, std::size_t len) {
  static const char kName[] = "\ncontent-length:";
  const std::size_t k = sizeof(kName) - 1;
  for (std::size_t i = 0; i + k <= len; ++i) {
    std::size_t j = 0;
    while (j < k && std::tolower(static_cast<unsigned char>(head[i + j])) == kName[j]) ++j;
    if (j == k) return std::strtol(head + i + k, nullptr, 10);
  }
  return 0;
}

}
}
