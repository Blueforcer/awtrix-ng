#pragma once

#include <cstdio>

namespace awtrix {
inline void writeLogLine(const char* line) {
  std::fputs(line, stderr);
  std::fputc('\n', stderr);
}
}
