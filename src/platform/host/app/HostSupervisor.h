#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace awtrix::hostapp {

enum class HostState : uint8_t { Starting, Running, Restarting, Stopped };
enum class StopReason : uint8_t { None, Quit, PortBusy, CannotStart };

struct HostView {
  HostState state = HostState::Stopped;
  StopReason reason = StopReason::None;
  int64_t restartInMs = 0;

  bool up() const { return state == HostState::Starting || state == HostState::Running; }
};

class HostProcess {
 public:
  virtual ~HostProcess() = default;
  virtual bool start(const std::vector<std::string>& args) = 0;
  virtual void stop() = 0;
};

class HostSupervisor {
 public:
  static constexpr int64_t kSettleMs = 2000;
  static constexpr int64_t kHealthyMs = 60000;
  static constexpr int64_t kBackoffMs[] = {5000, 10000, 60000};

  HostSupervisor(HostProcess& process, std::vector<std::string> args);

  void start(int64_t nowMs);
  void ended(int exitCode, int64_t nowMs);
  void wake(int64_t nowMs);
  void quit();

  HostState state() const { return state_; }
  StopReason reason() const { return reason_; }
  std::optional<int64_t> wakeAt() const;
  HostView view(int64_t nowMs) const;

 private:
  void launch(int64_t nowMs);

  HostProcess& process_;
  std::vector<std::string> args_;
  HostState state_ = HostState::Stopped;
  StopReason reason_ = StopReason::None;
  int64_t startedMs_ = 0;
  int64_t restartMs_ = 0;
  std::size_t failures_ = 0;
};

}
