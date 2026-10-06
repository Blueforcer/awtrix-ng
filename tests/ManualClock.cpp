#include "ManualClock.h"

#include <atomic>
#include <time.h>

namespace {
std::atomic<int64_t> offsetMs{0};
}

namespace awtrix_test {
void advanceClock(int64_t milliseconds) { offsetMs.fetch_add(milliseconds); }
}

extern "C" int __real_clock_gettime(clockid_t clock, struct timespec* value);

extern "C" int __wrap_clock_gettime(clockid_t clock, struct timespec* value) {
  const int result = __real_clock_gettime(clock, value);
  if (result || clock != CLOCK_MONOTONIC) return result;
  const int64_t offset = offsetMs.load();
  value->tv_sec += offset / 1000;
  value->tv_nsec += (offset % 1000) * 1000000;
  if (value->tv_nsec >= 1000000000) {
    ++value->tv_sec;
    value->tv_nsec -= 1000000000;
  }
  return 0;
}
