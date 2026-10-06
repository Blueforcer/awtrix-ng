#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/tc002/daemon/Service.h"

// The daemon's single poll loop. Services start in the order they were added and stop in reverse
// order, one at a time: the next service is asked to stop only after the previous one reports
// stopped() or its stop timeout passed. Signals arrive through a self-pipe; SIGCHLD reaps every
// exited child and offers it to the services, SIGTERM/SIGINT request an orderly stop and SIGHUP is
// logged and ignored.
namespace awtrix {
namespace tc002d {

class EventLoop {
 public:
  EventLoop();
  ~EventLoop();
  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;

  void add(Service& service, int64_t stopTimeoutMs = 10000);
  // False when a service refused to start; the stop sequence is then already running.
  bool start();
  void requestStop(const std::string& reason);
  // One iteration, waiting at most maxWaitMs. Returns false once the stop sequence completed.
  bool runOnce(int maxWaitMs);
  void run();

  bool stopping() const { return stopping_; }
  bool finished() const { return finished_; }
  const std::string& stopReason() const { return stopReason_; }
  unsigned unclaimedChildren() const { return unclaimedChildren_; }

 private:
  struct Entry {
    Service* service = nullptr;
    int64_t stopTimeoutMs = 0;
    bool started = false;
    bool stopRequested = false;
    int64_t stopDeadline = 0;
  };

  void drainSignals(int64_t nowMs);
  void reapChildren(int64_t nowMs);
  void advanceStop(int64_t nowMs);

  std::vector<Entry> entries_;
  std::size_t stopCursor_ = 0;
  bool stopping_ = false;
  bool finished_ = false;
  std::string stopReason_;
  unsigned unclaimedChildren_ = 0;
  int signalRead_ = -1;
};

}
}
