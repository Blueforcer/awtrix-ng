#pragma once

#include <sys/socket.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "platform/posix/UniqueFd.h"

namespace awtrix::posix {
struct SocketAddress {
  sockaddr_storage addr{};
  socklen_t size = 0;
  int family = 0;
};

// One detached lookup with its own pollable completion descriptor. Dropping the last caller's
// reference never waits for DNS; the worker owns the state until its late answer is discarded.
class Resolver {
 public:
  using Addresses = std::vector<SocketAddress>;
  using Lookup = std::function<Addresses()>;
  static std::shared_ptr<Resolver> start(Lookup lookup);
  static Addresses lookup(const std::string& host, const std::string& port, std::size_t maximum = SIZE_MAX);
  int fd() const { return wake_.get(); }
  // Non-blocking, once only. An empty answer means lookup failure.
  bool take(Addresses& out);

 private:
  Resolver();
  UniqueFd wake_;
  std::mutex mutex_;
  Addresses addresses_;
  bool ready_ = false;
};

enum class ConnectState { Failed, Pending, Connected };
struct ConnectResult {
  UniqueFd fd;
  ConnectState state = ConnectState::Failed;
  int error = 0;
};
// Tries addresses in order using non-blocking CLOEXEC sockets; advances next past each attempt.
// A pending socket still needs a poll plus SO_ERROR check before it is connected.
ConnectResult connectNext(const SocketAddress* addresses, std::size_t count, std::size_t& next);
inline ConnectResult connectNext(const Resolver::Addresses& addresses, std::size_t& next) {
  return connectNext(addresses.data(), addresses.size(), next);
}
// After writability, returns zero for a connected socket or the pending socket error.
int connectionError(int fd);
}
