#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <string>

#include "platform/tc002/contract/SupervisorProtocol.h"

namespace awtrix {

// Accepts only a connected AF_UNIX SOCK_SEQPACKET socket whose peer is root or this user.
bool validateSupervisorSocket(int descriptor, std::string& error);

// Runtime end of the awtrix-tc002d channel: an inherited AF_UNIX SOCK_SEQPACKET descriptor that
// carries one protocol message per datagram. Every call returns without waiting except flush().
class SupervisorLink {
 public:
  static constexpr int kDescriptor = tc002::kSupervisorFd;
  static constexpr std::size_t kMaxQueued = 32;
  static constexpr unsigned kMaxReceivedPerPoll = 32;

  SupervisorLink() = default;
  ~SupervisorLink();
  SupervisorLink(const SupervisorLink&) = delete;
  SupervisorLink& operator=(const SupervisorLink&) = delete;

  bool open(int descriptor, std::string& error);
  bool active() const { return fd_ >= 0; }
  bool closed() const { return closed_; }
  unsigned refused() const { return refused_; }

  // Delivers the datagrams waiting now and retries queued output. False once the peer is gone.
  bool poll(const std::function<void(const tc002::SupervisorMessage&)>& onMessage);
  bool send(std::string datagram);
  bool flush(int timeoutMs);

 private:
  void transmit();
  void lose();

  int fd_ = -1;
  bool closed_ = false;
  unsigned refused_ = 0;
  std::deque<std::string> queue_;
};

}
