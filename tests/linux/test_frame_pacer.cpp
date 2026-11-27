#include "platform/linux/remote/FramePacer.h"
#include "../support.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

using awtrix::remote::FramePacer;
using awtrix::test::check;
using awtrix::test::require;

namespace {

constexpr int64_t kPeriod = 24;

struct FakeClock {
  int64_t nowMs = 0;
  void sleepUntil(int64_t deadlineMs, int64_t oversleepMs) {
    if (deadlineMs > nowMs) nowMs = deadlineMs + oversleepMs;
  }
};

void noDriftWithOversleep() {
  FakeClock clock{987654};
  std::mt19937 random(7);
  std::uniform_int_distribution<int64_t> oversleep(0, 9), work(0, 6);
  FramePacer pacer(kPeriod);
  pacer.start(clock.nowMs);
  const int64_t first = pacer.tickMs();
  std::vector<int64_t> stamps;
  for (int tick = 0; tick < 10000; ++tick) {
    stamps.push_back(pacer.tickMs());
    clock.nowMs += work(random);
    clock.sleepUntil(pacer.advance(clock.nowMs), oversleep(random));
  }
  bool nominal = true;
  for (std::size_t i = 0; i < stamps.size(); ++i)
    nominal = nominal && stamps[i] == first + static_cast<int64_t>(i) * kPeriod;
  check(nominal, "every stamp is its nominal tick, start + n * period");
  check(pacer.tickMs() == first + 10000 * kPeriod, "10 000 ticks end exactly 10 000 periods after the start");
  check(pacer.skipped() == 0, "oversleeping less than a period skips no tick");
  check(clock.nowMs - pacer.tickMs() < kPeriod, "the wake time stays within one period of the nominal tick");
}

void slowFramesStayOnTheGrid() {
  FakeClock clock{42};
  std::mt19937 random(11);
  std::uniform_int_distribution<int64_t> oversleep(0, 9), work(0, 60);
  FramePacer pacer(kPeriod);
  pacer.start(clock.nowMs);
  const int64_t first = pacer.tickMs();
  int64_t last = first - kPeriod;
  bool grid = true, rising = true;
  for (int tick = 0; tick < 10000; ++tick) {
    grid = grid && (pacer.tickMs() - first) % kPeriod == 0;
    rising = rising && pacer.tickMs() > last;
    last = pacer.tickMs();
    clock.nowMs += work(random);
    clock.sleepUntil(pacer.advance(clock.nowMs), oversleep(random));
  }
  check(grid, "with frames longer than a period every stamp is still a nominal tick");
  check(rising, "the stamps only rise");
  check(pacer.skipped() > 0, "frames longer than two periods skip ticks");
  check(pacer.tickMs() == first + (10000 + pacer.skipped()) * kPeriod,
        "run and skipped ticks add up to the elapsed periods");
  check(clock.nowMs - pacer.tickMs() < kPeriod, "a late tick is less than one period behind the clock");
}

void lateWakeRunsAtOnce() {
  FramePacer pacer(kPeriod);
  pacer.start(1000);
  check(pacer.advance(1000 + kPeriod + 5) == 1000 + kPeriod, "a wake 5 ms past the next tick runs that tick at once");
  check(pacer.skipped() == 0, "a wake within one period after the tick skips nothing");
  check(pacer.advance(1000 + 2 * kPeriod - 1) == 1000 + 2 * kPeriod, "the following tick keeps the grid");
}

void stallSkipsToTheGrid() {
  FramePacer pacer(kPeriod);
  pacer.start(1000);
  const int64_t due = pacer.advance(1000 + 100);
  check(due == 1000 + 4 * kPeriod, "a 100 ms stall resumes at the last tick before now, got " + std::to_string(due));
  check(due <= 1000 + 100, "the resumed tick is due at once");
  check(pacer.skipped() == 3, "the three ticks missed in a 100 ms stall are skipped");
  const int64_t next = pacer.advance(due + 3);
  check(next == 1000 + 5 * kPeriod, "after a stall the ticks stay on the grid");
  check((pacer.advance(next + 10 * kPeriod + 23) - 1000) % kPeriod == 0, "a long stall lands on the grid as well");
  check(pacer.skipped() == 3 + 9, "a stall of ten periods skips nine ticks");
}

void earlyWakeDoesNotRepeat() {
  FramePacer pacer(kPeriod);
  pacer.start(5000);
  check(pacer.advance(4990) == 5000 + kPeriod, "a clock behind the tick still moves to the next tick");
  check(pacer.advance(5000) == 5000 + 2 * kPeriod, "every advance moves at least one tick");
  require(pacer.skipped() == 0, "nothing was skipped");
}

}

int main() {
  noDriftWithOversleep();
  slowFramesStayOnTheGrid();
  lateWakeRunsAtOnce();
  stallSkipsToTheGrid();
  earlyWakeDoesNotRepeat();
  return awtrix::test::finish("frame-pacer");
}
