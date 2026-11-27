#include "platform/host/app/HostSupervisor.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "platform/linux/hostd/Contract.h"

namespace awtrix::hostapp {

HostSupervisor::HostSupervisor(HostProcess& process, std::vector<std::string> args)
    : process_(process), args_(std::move(args)) {}

void HostSupervisor::start(int64_t nowMs) {
  if (state_ == HostState::Starting || state_ == HostState::Running) return;
  failures_ = 0;
  launch(nowMs);
}

void HostSupervisor::ended(int exitCode, int64_t nowMs) {
  if (state_ != HostState::Starting && state_ != HostState::Running) return;
  if (exitCode == hostd::kExitPortBusy) {
    state_ = HostState::Stopped;
    reason_ = StopReason::PortBusy;
    return;
  }
  if (nowMs - startedMs_ >= kHealthyMs) failures_ = 0;
  const std::size_t step = std::min(failures_, std::size(kBackoffMs) - 1);
  ++failures_;
  restartMs_ = nowMs + kBackoffMs[step];
  state_ = HostState::Restarting;
}

void HostSupervisor::wake(int64_t nowMs) {
  if (state_ == HostState::Starting && nowMs - startedMs_ >= kSettleMs)
    state_ = HostState::Running;
  else if (state_ == HostState::Restarting && nowMs >= restartMs_)
    launch(nowMs);
}

void HostSupervisor::quit() {
  state_ = HostState::Stopped;
  reason_ = StopReason::Quit;
  process_.stop();
}

std::optional<int64_t> HostSupervisor::wakeAt() const {
  if (state_ == HostState::Starting) return startedMs_ + kSettleMs;
  if (state_ == HostState::Restarting) return restartMs_;
  return std::nullopt;
}

HostView HostSupervisor::view(int64_t nowMs) const {
  HostView view;
  view.state = state_;
  view.reason = reason_;
  if (state_ == HostState::Restarting) view.restartInMs = std::max<int64_t>(0, restartMs_ - nowMs);
  return view;
}

void HostSupervisor::launch(int64_t nowMs) {
  reason_ = StopReason::None;
  if (!process_.start(args_)) {
    state_ = HostState::Stopped;
    reason_ = StopReason::CannotStart;
    return;
  }
  state_ = HostState::Starting;
  startedMs_ = nowMs;
}

}
