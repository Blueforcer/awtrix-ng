#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "platform/host/Socket.h"

namespace awtrix::hostd {

class ClockStore;

// Finds clocks on the network with the clocks' UDP discovery (FIND_AWTRIXNG on 4210, answers on
// 4211) and offers this host to each of them on its discovery port, every half minute. Each paired
// clock is also told where its process here listens, in an offer signed with its key.
class Discovery {
 public:
  static constexpr int64_t kEveryMs = 30000;
  static constexpr uint16_t kClockPort = 4210;
  static constexpr uint16_t kReplyPort = 4211;

  explicit Discovery(uint16_t clockPort = kClockPort, uint16_t replyPort = kReplyPort);
  ~Discovery();
  // name and port are what the clocks are told: this host's name and its web port.
  bool begin(std::string name, uint16_t port);
  void tick(int64_t nowMs, const ClockStore& store);
  bool listening() const { return socket_ != os::kNoSocket; }

 private:
  static constexpr std::size_t kMaxNoted = 32;

  bool listen();
  void ask(const ClockStore& store);
  void offer(uint32_t address);
  void send(const uint8_t* packet, std::size_t length, uint32_t address);
  void note(uint32_t address);

  os::Socket socket_ = os::kNoSocket;
  uint16_t clockPort_;
  uint16_t replyPort_;
  bool started_ = false;
  std::string name_;
  uint16_t port_ = 0;
  int64_t nextAskMs_ = 0;
  std::vector<uint32_t> noted_;
};

}
