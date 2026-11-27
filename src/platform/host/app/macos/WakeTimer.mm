#include "platform/host/app/macos/WakeTimer.h"

#include <algorithm>
#include <utility>

namespace awtrix::hostapp {
namespace {

constexpr uint64_t kLeewayNs = 20 * NSEC_PER_MSEC;

}

WakeTimer::WakeTimer(dispatch_queue_t queue, std::function<void()> fire)
    : fire_(std::move(fire)), timer_(dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue)) {
  dispatch_source_set_event_handler(timer_, ^{
    fire_();
  });
  dispatch_source_set_timer(timer_, DISPATCH_TIME_FOREVER, DISPATCH_TIME_FOREVER, kLeewayNs);
  dispatch_resume(timer_);
}

WakeTimer::~WakeTimer() { dispatch_source_cancel(timer_); }

void WakeTimer::at(std::optional<int64_t> wakeMs, int64_t nowMs) {
  const dispatch_time_t start =
      wakeMs ? dispatch_time(DISPATCH_TIME_NOW, std::max<int64_t>(0, *wakeMs - nowMs) * static_cast<int64_t>(NSEC_PER_MSEC))
             : DISPATCH_TIME_FOREVER;
  dispatch_source_set_timer(timer_, start, DISPATCH_TIME_FOREVER, kLeewayNs);
}

}
