#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "platform/tc002/contract/SupervisorProtocol.h"

namespace awtrix::tc002d::wifi {

// Coalesced scan requests, result cache and supplicant scan deadlines.
class WifiScan {
 public:
  using Done = std::function<void(const std::vector<tc002::WifiNetwork>&)>;
  bool start(int64_t now);
  void wait(Done done);
  void finish(std::vector<tc002::WifiNetwork> networks, bool fresh, int64_t now);
  void resetControl();
  void commandReply(std::string_view reply, int64_t now);
  void resultsEvent(bool allowUnrequested);
  bool timedOut(int64_t now) const;
  void onTime(int64_t now);
  int64_t nextDeadlineMs() const;
  bool active() const { return active_; }
  bool commandWanted() const { return active_ && !sent_; }
  bool resultsWanted() const { return wantResults_; }
  void requestResults() { wantResults_ = true; }
  void takeResults() { wantResults_ = false; }
  bool hasResults() const { return lastAt_ >= 0; }
  bool contains(std::string_view ssid) const;
  const std::vector<tc002::WifiNetwork>& networks() const { return networks_; }
  const std::string& error() const { return error_; }
  void clearError() { error_.clear(); }
  void unavailable() { error_ = "scan-unavailable-in-access-point"; }
  std::string json(int64_t now) const;

 private:
  bool active_ = false, sent_ = false, wantResults_ = false;
  int64_t giveUpAt_ = -1, eventDeadline_ = -1, lastAt_ = -1;
  std::string error_;
  std::vector<Done> waiters_;
  std::vector<tc002::WifiNetwork> networks_;
};

}  // namespace awtrix::tc002d::wifi
