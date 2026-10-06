#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/tc002/daemon/Service.h"
#include "platform/tc002/daemon/wifi/WpaControl.h"

namespace awtrix::tc002d::wifi {

// One command in flight, plus a separately attached event socket.
class SupplicantSession {
 public:
  enum class Command : uint8_t { None, Status, SignalPoll, Scan, ScanResults, AccessPointClients };
  enum class Timeout { None, Reopened, Reconnect, Unresponsive };

  void start(int64_t now);
  bool open(const std::string& directory, const std::string& server, int64_t now);
  void close();
  void scheduleRetry(int64_t now);
  void cancelRetry();
  bool retryDue(int64_t now) const;
  bool gaveUp(int64_t now);
  bool isOpen() const { return command_.isOpen(); }
  bool ready() const { return isOpen() && pending_ == Command::None; }
  bool attached() const { return monitorAttached_; }
  bool send(Command kind, const char* text, int64_t now);
  bool commandFd(int fd) const { return command_.isOpen() && fd == command_.fd(); }
  bool monitorFd(int fd) const { return monitor_.isOpen() && fd == monitor_.fd(); }
  bool receiveReply(std::string& message, Command& kind);
  bool receiveMonitor(std::string& message);
  void pollInterest(std::vector<PollInterest>& out) const;
  Timeout expire(int64_t now, const std::string& directory, const std::string& controlDirectory, Command& expired);
  void attachTimer(int64_t now);
  int64_t nextDeadlineMs(int64_t now, bool commandWanted) const;

 private:
  WpaControlSocket command_, monitor_;
  Command pending_ = Command::None;
  int64_t commandDeadline_ = -1;
  unsigned commandTimeouts_ = 0;
  bool monitorAttached_ = false;
  int64_t monitorDeadline_ = -1;
  int64_t retryAt_ = -1, giveUpAt_ = -1;
};

}  // namespace awtrix::tc002d::wifi
