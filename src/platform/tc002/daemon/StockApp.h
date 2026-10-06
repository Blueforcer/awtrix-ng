#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "platform/tc002/daemon/Service.h"

// Init service "hciattach" (/res/bin/hciattach) attaches the Bluetooth controller the
// manufacturer app uses for BLE setup; the daemon keeps it off.
namespace awtrix {
namespace tc002d {

struct StockWatchOptions {
  std::string procRoot = "/proc";
  std::string setprop = "/bin/setprop";
  int64_t intervalMs = 5000;
};

// Every intervalMs the process table is checked for hciattach. A process that appears gets one
// "ctl.stop hciattach" through setprop with the inherited property workspace; if the same process
// is still there at the next check it gets SIGKILL, as it does right away without a property
// workspace. Only transitions are logged.
class StockWatch : public Service {
 public:
  enum class Phase { Off, Stopping, Killed, Stuck };

  explicit StockWatch(StockWatchOptions options);

  const char* name() const override { return "stock-watch"; }
  bool start(int64_t nowMs) override;
  int64_t nextDeadlineMs() const override { return stopping_ ? -1 : nextCheck_; }
  void onTime(int64_t nowMs) override;
  bool onChildExit(pid_t pid, int status, int64_t nowMs) override;
  void requestStop(int64_t nowMs) override;
  bool stopped() const override { return setprop_ <= 0; }

  Phase bluetooth() const { return phase_; }
  unsigned checks() const { return checks_; }

 private:
  void check();
  void stopAppeared(const std::vector<pid_t>& pids);

  StockWatchOptions options_;
  Phase phase_ = Phase::Off;
  pid_t setprop_ = -1;
  std::vector<pid_t> seen_;
  int64_t nextCheck_ = -1;
  bool stopping_ = false;
  bool blind_ = false;
  unsigned checks_ = 0;
};

}
}
