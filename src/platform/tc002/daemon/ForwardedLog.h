#pragma once

#include <deque>
#include <string_view>

#include "platform/tc002/contract/SupervisorProtocol.h"

namespace awtrix::tc002d {
class RuntimeChild;

// Hands the daemon log to the runtime from its construction on (Log::forwardTo): lines from before
// the runtime exists wait in a bounded backlog, so the update notes of a start reach the first
// runtime too. attach() binds a runtime until the returned guard ends, which must happen before
// the runtime ends.
class ForwardedLog {
 public:
  class Attachment {
   public:
    ~Attachment() { log_.runtime_ = nullptr; }
    Attachment(const Attachment&) = delete;
    Attachment& operator=(const Attachment&) = delete;

   private:
    friend class ForwardedLog;
    explicit Attachment(ForwardedLog& log) : log_(log) {}
    ForwardedLog& log_;
  };

  ForwardedLog();
  ~ForwardedLog();
  ForwardedLog(const ForwardedLog&) = delete;
  ForwardedLog& operator=(const ForwardedLog&) = delete;
  [[nodiscard]] Attachment attach(RuntimeChild& runtime);

 private:
  void forward(const char* component, std::string_view text);
  RuntimeChild* runtime_ = nullptr;
  std::deque<tc002::LogLine> early_;
};
}  // namespace awtrix::tc002d
