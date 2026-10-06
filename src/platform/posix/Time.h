#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace awtrix::posix {

inline int64_t monotonicUs() {
  timespec now{};
  if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
  return static_cast<int64_t>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
}

inline int64_t monotonicMs() {
  const int64_t now = monotonicUs();
  return now < 0 ? now : now / 1000;
}

inline std::string wallClock(std::time_t now = std::time(nullptr)) {
  std::tm utc{};
  char text[32]{};
  if (!::gmtime_r(&now, &utc) || !std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &utc))
    return {};
  return text;
}

}  // namespace awtrix::posix
