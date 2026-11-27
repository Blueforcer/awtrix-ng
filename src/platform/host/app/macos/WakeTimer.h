#pragma once

#include <dispatch/dispatch.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace awtrix::hostapp {

class WakeTimer {
 public:
  WakeTimer(dispatch_queue_t queue, std::function<void()> fire);
  ~WakeTimer();
  WakeTimer(const WakeTimer&) = delete;
  WakeTimer& operator=(const WakeTimer&) = delete;

  void at(std::optional<int64_t> wakeMs, int64_t nowMs);

 private:
  std::function<void()> fire_;
  dispatch_source_t timer_;
};

}
