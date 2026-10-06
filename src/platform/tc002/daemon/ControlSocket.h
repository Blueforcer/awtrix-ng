#pragma once

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/tc002/contract/tc002_layout.h"
#include "platform/tc002/daemon/ControlRegistry.h"
#include "platform/posix/Files.h"
#include "platform/tc002/daemon/Service.h"

// Server side of the framing described in ControlRegistry.h. Each connection is a small
// non-blocking state machine (read frame, run handler, write frame, close) with a deadline.
// Built-in command: "commands" lists every registered command as a JSON array.
namespace awtrix {
namespace tc002d {

struct ControlOptions {
  std::string path = TC002_VOLATILE_DIR TC002_CONTROL_SOCKET;
  uid_t peerUid = 0;
  int64_t clientTimeoutMs = 5000;
  int64_t bindRetryMs = 5000;
  unsigned maxClients = 4;
};

class ControlSocket : public Service, public ControlRegistry {
 public:
  static constexpr std::size_t kMaxRequest = 4096;
  static constexpr std::size_t kMaxReply = 65536;

  explicit ControlSocket(ControlOptions options);
  ~ControlSocket() override;

  void add(std::string command, Handler handler) override;

  const char* name() const override { return "control"; }
  bool start(int64_t nowMs) override;
  void pollInterest(std::vector<PollInterest>& out) const override;
  void onReady(int fd, short revents, int64_t nowMs) override;
  int64_t nextDeadlineMs() const override;
  void onTime(int64_t nowMs) override;
  void requestStop(int64_t nowMs) override;
  bool stopped() const override { return stopping_ && !listener_.valid() && clients_.empty(); }

  std::string dispatch(std::string_view request) const;
  bool listening() const { return listener_.valid(); }
  std::size_t clients() const { return clients_.size(); }

 private:
  struct Client {
    posix::UniqueFd fd;
    std::string input;
    std::string output;
    std::size_t sent = 0;
    int64_t deadline = 0;
    bool refused = false;
  };

  bool bindListener(int64_t nowMs);
  void acceptClients(int64_t nowMs);
  void serviceClient(Client& client, short revents);
  void closeListener();

  ControlOptions options_;
  posix::UniqueFd listener_;
  bool stopping_ = false;
  int64_t retryAt_ = -1;
  unsigned bindFailures_ = 0;
  std::vector<std::pair<std::string, Handler>> handlers_;
  std::vector<std::unique_ptr<Client>> clients_;
};

std::string encodeControlFrame(std::string_view payload);
// Blocking client used by `awtrix-tc002d ctl`, bounded by timeoutMs.
bool controlRequest(const std::string& path, std::string_view request, std::string& reply, int timeoutMs,
                    std::string& error);

}
}
