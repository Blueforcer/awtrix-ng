#include "platform/host/Timer.h"
#include "../support.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "system/MonotonicClock.h"

using awtrix::monotonicUs;
using awtrix::test::check;
using awtrix::test::require;

namespace {

constexpr int64_t kPeriodUs = 10000;
constexpr int kTicks = 40;

void pastDeadlineReturnsAtOnce() {
  const int64_t before = monotonicUs();
  awtrix::os::sleepUntil(before - 5000);
  awtrix::os::sleepUntil(0);
  check(monotonicUs() - before < 2000, "a deadline in the past returns at once");
}

void ticksWakeOnTime() {
  std::vector<int64_t> late;
  const int64_t start = monotonicUs();
  for (int tick = 1; tick <= kTicks; ++tick) {
    const int64_t deadline = start + tick * kPeriodUs;
    awtrix::os::sleepUntil(deadline);
    late.push_back(monotonicUs() - deadline);
  }
  require(late.size() == static_cast<std::size_t>(kTicks), "every tick was measured");
  const int64_t early = std::count_if(late.begin(), late.end(), [](int64_t us) { return us < 0; });
  check(early == 0, "no wake comes before its deadline, early: " + std::to_string(early));
  std::sort(late.begin(), late.end());
  const int64_t median = late[late.size() / 2];
  std::printf("sleepUntil late us: min %lld median %lld max %lld\n", static_cast<long long>(late.front()),
              static_cast<long long>(median), static_cast<long long>(late.back()));
  check(median < 5000, "half of the wakes come within 5 ms of the deadline, median " + std::to_string(median) + " us");
}

}

int main() {
  pastDeadlineReturnsAtOnce();
  ticksWakeOnTime();
  return awtrix::test::finish("timer");
}
