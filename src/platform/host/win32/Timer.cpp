#include "platform/host/Timer.h"

#include <timeapi.h>

#include <chrono>

namespace awtrix::os {
namespace {

int64_t nowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

void keepFullSpeed() {
  PROCESS_POWER_THROTTLING_STATE state{};
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
  state.StateMask = 0;
  if (!::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof state)) {
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    ::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof state);
  }
}

class Waiter {
 public:
  Waiter() {
    keepFullSpeed();
    timer_ = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer_) return;
    finePeriod_ = ::timeBeginPeriod(1) == TIMERR_NOERROR;
    timer_ = ::CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
  }
  ~Waiter() {
    if (timer_) ::CloseHandle(timer_);
    if (finePeriod_) ::timeEndPeriod(1);
  }
  Waiter(const Waiter&) = delete;
  Waiter& operator=(const Waiter&) = delete;

  void wait(int64_t us) {
    LARGE_INTEGER due{};
    due.QuadPart = -us * 10;
    if (timer_ && ::SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE) &&
        ::WaitForSingleObject(timer_, INFINITE) == WAIT_OBJECT_0)
      return;
    ::Sleep(static_cast<DWORD>((us + 999) / 1000));
  }

 private:
  HANDLE timer_ = nullptr;
  bool finePeriod_ = false;
};

}

void sleepUntil(int64_t monotonicUs) {
  thread_local Waiter waiter;
  for (int64_t left; (left = monotonicUs - nowUs()) > 0;) waiter.wait(left);
}

}
