#pragma once

#include <chrono>
#include <cstdint>

// Microseconds since the first call rather than since boot; callers only ever take differences.
inline int64_t esp_timer_get_time() {
  using namespace std::chrono;
  static const steady_clock::time_point t0 = steady_clock::now();
  return static_cast<int64_t>(duration_cast<microseconds>(steady_clock::now() - t0).count());
}
