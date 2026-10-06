#include "platform/tc002/daemon/wifi/SupplicantSession.h"

#include <poll.h>

namespace awtrix::tc002d::wifi {
namespace {
constexpr int64_t kRetryMs = 100;
constexpr int64_t kGiveUpMs = 10000;
constexpr int64_t kCommandMs = 2000;
}

void SupplicantSession::start(int64_t now) {
  commandTimeouts_ = 0;
  retryAt_ = now + kRetryMs;
  giveUpAt_ = now + kGiveUpMs;
}

bool SupplicantSession::open(const std::string& directory, const std::string& server, int64_t now) {
  int error = 0;
  if (!command_.open(directory + "/cmd", server, error) ||
      !monitor_.open(directory + "/mon", server, error)) return false;
  cancelRetry();
  monitorAttached_ = false;
  if (!monitor_.send("ATTACH")) return false;
  monitorDeadline_ = now + kCommandMs;
  return true;
}

void SupplicantSession::close() {
  command_.close();
  monitor_.close();
  pending_ = Command::None;
  commandDeadline_ = monitorDeadline_ = -1;
  monitorAttached_ = false;
}

void SupplicantSession::scheduleRetry(int64_t now) {
  retryAt_ = now + kRetryMs;
  if (giveUpAt_ < 0) giveUpAt_ = now + kGiveUpMs;
}

void SupplicantSession::cancelRetry() { retryAt_ = giveUpAt_ = -1; }

bool SupplicantSession::retryDue(int64_t now) const { return retryAt_ >= 0 && now >= retryAt_; }

bool SupplicantSession::gaveUp(int64_t now) {
  if (giveUpAt_ < 0 || now < giveUpAt_) return false;
  cancelRetry();
  return true;
}

bool SupplicantSession::send(Command kind, const char* text, int64_t now) {
  pending_ = kind;
  if (!command_.send(text)) return false;
  commandDeadline_ = now + kCommandMs;
  return true;
}

bool SupplicantSession::receiveReply(std::string& message, Command& kind) {
  if (!command_.receive(message)) return false;
  kind = pending_;
  if (kind != Command::None) {
    pending_ = Command::None;
    commandDeadline_ = -1;
    commandTimeouts_ = 0;
  }
  return true;
}

bool SupplicantSession::receiveMonitor(std::string& message) {
  if (!monitor_.receive(message)) return false;
  if (!monitorAttached_ && message.compare(0, 2, "OK") == 0) {
    monitorAttached_ = true;
    monitorDeadline_ = -1;
  }
  return true;
}

void SupplicantSession::pollInterest(std::vector<PollInterest>& out) const {
  if (command_.isOpen()) out.push_back({command_.fd(), POLLIN});
  if (monitor_.isOpen()) out.push_back({monitor_.fd(), POLLIN});
}

SupplicantSession::Timeout SupplicantSession::expire(int64_t now, const std::string& directory,
                                                    const std::string& controlDirectory, Command& expired) {
  if (pending_ == Command::None || now < commandDeadline_) return Timeout::None;
  expired = pending_;
  pending_ = Command::None;
  commandDeadline_ = -1;
  ++commandTimeouts_;
  command_.close();
  if (commandTimeouts_ >= 3) return Timeout::Unresponsive;
  int error = 0;
  if (!command_.open(directory + "/cmd", controlDirectory + "/wlan0", error)) return Timeout::Reconnect;
  return Timeout::Reopened;
}

void SupplicantSession::attachTimer(int64_t now) {
  if (monitor_.isOpen() && !monitorAttached_ && monitorDeadline_ >= 0 && now >= monitorDeadline_) {
    monitorDeadline_ = now + kCommandMs;
    monitor_.send("ATTACH");
  }
}

int64_t SupplicantSession::nextDeadlineMs(int64_t now, bool commandWanted) const {
  if (!isOpen()) {
    if (retryAt_ >= 0 && (giveUpAt_ < 0 || retryAt_ < giveUpAt_)) return retryAt_;
    return giveUpAt_;
  }
  int64_t best = pending_ != Command::None ? commandDeadline_ : commandWanted ? now : -1;
  if (!monitorAttached_ && monitorDeadline_ >= 0 && (best < 0 || monitorDeadline_ < best))
    best = monitorDeadline_;
  return best;
}

}  // namespace awtrix::tc002d::wifi
