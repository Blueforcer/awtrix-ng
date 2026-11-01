#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "platform/linux/LinuxMemory.h"
#include "platform/linux/net/TransferQueue.h"
#include "platform/tc002/voice/ClipStore.h"

namespace awtrix::tc002::voice {

class VoiceTransfers {
 public:
  enum class Kind { Send, Fetch };
  using SendData = std::variant<ClipId, std::string>;
  struct SendOptions {
    std::string url, method, headers, as, field, fields, find;
    int keep = 0, cap = 0;
    uint32_t timeoutSec = 0;
  };
  struct Done {
    std::string app;
    Kind kind = Kind::Send;
    int number = 0, status = 0;
    ClipId clip = 0;
    std::optional<std::string> text;
  };

  VoiceTransfers(ClipStore& clips, MemoryRoom memory, net::FillRequest fill);
  VoiceTransfers(const VoiceTransfers&) = delete;
  VoiceTransfers& operator=(const VoiceTransfers&) = delete;

  int send(const std::string& app, SendData data, const SendOptions& options);
  int fetch(const std::string& app, const std::string& url, const std::string& method, std::string body,
            const std::string& headers, uint32_t timeoutSec);
  std::vector<Done> cancel(const std::string& app);
  std::optional<Done> poll();
  bool holds(const std::string& app) const;
  bool empty() const { return transfers_.empty(); }

 private:
  struct Transfer {
    std::string app;
    Kind kind = Kind::Send;
    ClipId clip = 0;
  };
  static Done report(std::string app, Kind kind, int number, int status);
  int start(net::TransferRequest request, Kind kind, ClipId clip);
  std::optional<Transfer> take(int number);
  std::optional<Done> finish(net::TransferResult& result);

  ClipStore& clips_;
  const MemoryRoom memory_;
  net::TransferQueue queue_;
  std::map<int, Transfer> transfers_;
  int number_ = 0;
};

}
