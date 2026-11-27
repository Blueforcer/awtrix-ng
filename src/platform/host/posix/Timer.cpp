#include "platform/host/Timer.h"

#include <cerrno>
#include <ctime>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#include <sys/event.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#endif

#include "platform/posix/Time.h"

namespace awtrix::os {

#if defined(__APPLE__)

namespace {

struct TimerQueue {
  TimerQueue() {
    mach_timebase_info_data_t base{};
    if (::mach_timebase_info(&base) == KERN_SUCCESS && base.numer && base.denom) {
      numer = base.numer;
      denom = base.denom;
    }
  }
  ~TimerQueue() {
    if (fd >= 0) ::close(fd);
  }
  TimerQueue(const TimerQueue&) = delete;
  TimerQueue& operator=(const TimerQueue&) = delete;

  uint64_t machAfter(int64_t us) const {
    return ::mach_absolute_time() + static_cast<uint64_t>(us) * 1000u * denom / numer;
  }

  int fd = ::kqueue();
  uint32_t numer = 1;
  uint32_t denom = 1;
};

}

void sleepUntil(int64_t monotonicUs) {
  thread_local TimerQueue queue;
  for (int64_t left; (left = monotonicUs - posix::monotonicUs()) > 0;) {
    if (queue.fd < 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(left));
      continue;
    }
    struct kevent timer;
    EV_SET(&timer, 1, EVFILT_TIMER, EV_ADD | EV_ONESHOT, NOTE_MACHTIME | NOTE_ABSOLUTE | NOTE_CRITICAL,
           static_cast<intptr_t>(queue.machAfter(left)), nullptr);
    struct kevent fired{};
    const int got = ::kevent(queue.fd, &timer, 1, &fired, 1, nullptr);
    if ((got < 0 && errno != EINTR) || (got > 0 && (fired.flags & EV_ERROR)))
      std::this_thread::sleep_for(std::chrono::microseconds(left));
  }
}

#else

void sleepUntil(int64_t monotonicUs) {
  if (monotonicUs <= 0) return;
  timespec at{};
  at.tv_sec = static_cast<time_t>(monotonicUs / 1000000);
  at.tv_nsec = static_cast<long>(monotonicUs % 1000000) * 1000;
  while (::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &at, nullptr) == EINTR) {
  }
}

#endif

}
