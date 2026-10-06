#pragma once

#include <deque>
#include <string>
#include <string_view>

#include "platform/posix/Files.h"
#include "platform/tc002/contract/SupervisorProtocol.h"

namespace awtrix::tc002d {

// Runtime stdout to daemon.log; selected daemon components to the current runtime.
class RuntimeLog {
 public:
  static constexpr std::size_t kBacklog = 16;
  static bool forwarded(std::string_view component);
  void line(int channel, const char* component, std::string_view text);
  void hello(int channel);
  void disconnect() { live_ = false; }
  void newOutput() { outputBuffer_.clear(); live_ = false; }
  void readOutput(posix::UniqueFd& output, bool final);

 private:
  void keep(tc002::LogLine line);
  void send(int channel, const tc002::LogLine& line);
  bool live_ = false;
  std::deque<tc002::LogLine> backlog_;
  std::string outputBuffer_;
};

}  // namespace awtrix::tc002d
