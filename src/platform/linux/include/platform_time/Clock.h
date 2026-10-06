#pragma once

#if defined(_WIN32)
#include <chrono>
#include <cstdint>

namespace awtrix {
inline int64_t monotonicUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline int64_t monotonicMs() { return monotonicUs() / 1000; }
}
#else
#include "platform/posix/Time.h"
namespace awtrix {
using posix::monotonicUs;
using posix::monotonicMs;
}
#endif
