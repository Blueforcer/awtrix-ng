#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/api/JsonWriter.h"
#include "platform/posix/UniqueFd.h"
#include "platform/tc002/daemon/Service.h"

// One program of the owner's choice, started with the daemon: a root-owned executable regular
// file at a fixed path in the private state directory, placed over USB ADB. The web UI and API
// cannot reach that directory, so running it needs the same physical access as root ADB.
namespace awtrix {
namespace tc002d {

struct AutostartOptions {
  std::string path;
  uid_t owner = 0;
  std::vector<std::string> environment;
  // Waits before restart n (the last entry repeats) after an exit other than 0.
  std::vector<int64_t> backoffMs{1000, 5000, 30000, 60000};
  // A run that lasted this long ends the failure streak.
  int64_t stableMs = 60000;
  // Consecutive failures after which it stays off until the daemon starts again.
  unsigned maxFailures = 5;
  int64_t stopGraceMs = 3000;
  // Output lines logged per window; the rest are counted.
  unsigned logBurst = 100;
  int64_t logWindowMs = 600000;
};

// The program runs in its own session with stdin on /dev/null and stdout and stderr in the daemon
// log (component "autostart"), read until the last process holding them closes them. It gets no
// hardware descriptor and dies with the daemon. Exit 0 ends it for good; any other end restarts it
// with backoff until maxFailures in a row.
class AutostartService : public Service {
 public:
  enum class State { None, Refused, Waiting, Running, Stopping, Finished, GaveUp };

  explicit AutostartService(AutostartOptions options);
  ~AutostartService() override;

  const char* name() const override { return "autostart"; }
  bool start(int64_t nowMs) override;
  void pollInterest(std::vector<PollInterest>& out) const override;
  void onReady(int fd, short revents, int64_t nowMs) override;
  int64_t nextDeadlineMs() const override;
  void onTime(int64_t nowMs) override;
  bool onChildExit(pid_t pid, int status, int64_t nowMs) override;
  void requestStop(int64_t nowMs) override;
  bool stopped() const override { return pid_ <= 0; }

  // Stops a running program, checks the file again and starts it with a fresh failure count.
  void restart(int64_t nowMs);
  void appendStatus(api::JsonWriter& json, int64_t nowMs) const;

  static const char* stateName(State state);
  State state() const { return state_; }
  pid_t pid() const { return pid_; }
  unsigned starts() const { return starts_; }
  const std::string& refusal() const { return refusal_; }

 private:
  // Empty when the file may run; otherwise why not. None when there is no file at all.
  std::string inspect(bool& present) const;
  void launch(int64_t nowMs);
  void signalGroup(int signal);
  void readOutput(bool final, int64_t nowMs);
  void emit(std::string_view text, int64_t nowMs);
  void closeWindow(int64_t nowMs);

  AutostartOptions options_;
  State state_ = State::None;
  pid_t pid_ = -1;
  posix::UniqueFd output_;
  std::string buffer_;
  std::string refusal_;
  std::string lastExit_;
  unsigned starts_ = 0;
  unsigned failures_ = 0;
  int64_t startedAt_ = 0;
  int64_t launchAt_ = -1;
  int64_t killAt_ = -1;
  bool stopping_ = false;
  bool restartPending_ = false;
  int64_t windowStart_ = 0;
  unsigned windowLines_ = 0;
  unsigned suppressed_ = 0;
};

}
}
