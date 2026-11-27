#pragma once

#include <cstdio>
#include <ctime>

namespace awtrix {
inline void writeLogLine(const char* line) {
  std::fputs(line, stderr);
  std::fputc('\n', stderr);
}

inline void systemLocalTime(std::time_t now, std::tm& out) {
#if defined(_WIN32)
  localtime_s(&out, &now);
#else
  localtime_r(&now, &out);
#endif
}

using LogClock = void (*)(std::time_t now, std::tm& out);
inline LogClock g_logClock = nullptr;

inline void logLocalTime(std::time_t now, std::tm& out) {
  if (g_logClock) g_logClock(now, out);
  else systemLocalTime(now, out);
}
}
