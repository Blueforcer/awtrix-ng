#include "platform/host/app/macos/HostDriver.h"

#include <chrono>
#include <utility>

namespace awtrix::hostapp {

HostDriver::HostDriver(std::vector<std::string> args, dispatch_queue_t queue, Changed changed)
    : changed_(std::move(changed)),
      process_(queue,
               [this](int exitCode) {
                 supervisor_.ended(exitCode, nowMs());
                 settle();
               }),
      supervisor_(process_, std::move(args)),
      timer_(queue, [this] {
        supervisor_.wake(nowMs());
        settle();
      }) {}

void HostDriver::start() {
  supervisor_.start(nowMs());
  settle();
}

void HostDriver::quit() {
  supervisor_.quit();
  settle();
}

HostView HostDriver::view() const { return supervisor_.view(nowMs()); }

int64_t HostDriver::nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void HostDriver::settle() {
  timer_.at(supervisor_.wakeAt(), nowMs());
  if (changed_) changed_();
}

}
