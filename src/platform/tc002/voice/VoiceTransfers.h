#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "platform/linux/net/TransferQueue.h"
#include "platform/tc002/voice/ClipStore.h"
#include "platform/tc002/voice/VoiceEvent.h"

namespace awtrix::tc002::voice {

class VoiceTransfers {
 public:
  struct Options {
    std::string url, method, headers, as, field, fields, find;
    int keep = 0, cap = 0;
    uint32_t timeoutSec = 0;
  };

  VoiceTransfers(ClipStore& clips, net::FillRequest fill);
  VoiceTransfers(const VoiceTransfers&) = delete;
  VoiceTransfers& operator=(const VoiceTransfers&) = delete;

  int send(const std::string& app, ClipId id, const Options& options);
  int fetch(const std::string& app, std::string body, const Options& options);
  std::vector<VoiceEvent> cancel(const std::string& app);
  std::optional<VoiceEvent> poll();
  void stop() { queue_.stop(); }
  bool holds(const std::string& app) const;
  bool holdsAny(const std::function<bool(const std::string&)>& app) const;

 private:
  struct Transfer {
    std::string app;
    VoiceEvent::Kind kind = VoiceEvent::Kind::Send;
    ClipId clip = 0;
    std::size_t bodyBytes = 0;
  };
  int start(net::TransferRequest request, VoiceEvent::Kind kind, ClipId clip);
  Transfer take(int number);
  VoiceEvent finish(net::TransferResult& result);

  ClipStore& clips_;
  net::TransferQueue queue_;
  std::map<int, Transfer> transfers_;
  std::size_t bodyBytes_ = 0;
  int number_ = 0;
};

}
