#include "platform/host/app/macos/DispatchHostProcess.h"

#include <utility>

namespace awtrix::hostapp {

DispatchHostProcess::DispatchHostProcess(dispatch_queue_t queue, Ended ended)
    : queue_(queue), ended_(std::move(ended)) {}

DispatchHostProcess::~DispatchHostProcess() { stop(); }

bool DispatchHostProcess::start(const std::vector<std::string>& args) {
  if (process_.running() || !process_.start(args)) return false;
  exit_ = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, static_cast<uintptr_t>(process_.id()), DISPATCH_PROC_EXIT,
                                 queue_);
  if (!exit_) {
    process_.stop();
    return false;
  }
  dispatch_source_set_event_handler(exit_, ^{
    exited();
  });
  dispatch_resume(exit_);
  return true;
}

void DispatchHostProcess::stop() {
  unwatch();
  process_.stop();
}

void DispatchHostProcess::exited() {
  unwatch();
  int code = -1;
  if (!process_.wait(kReapMs, code)) process_.stop(0);
  ended_(code);
}

void DispatchHostProcess::unwatch() {
  if (!exit_) return;
  dispatch_source_cancel(exit_);
  exit_ = nullptr;
}

}
