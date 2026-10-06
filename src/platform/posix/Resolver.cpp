#include "platform/posix/Resolver.h"

#include <netdb.h>
#include <sys/eventfd.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <system_error>
#include <thread>

namespace awtrix::posix {
Resolver::Resolver() : wake_(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {}

std::shared_ptr<Resolver> Resolver::start(Lookup lookup) {
  std::shared_ptr<Resolver> result(new Resolver);
  if (!result->wake_.valid()) return {};
  try {
    std::thread([result, lookup = std::move(lookup)] {
      Addresses addresses;
      try { addresses = lookup(); } catch (...) { addresses.clear(); }
      {
        std::lock_guard<std::mutex> lock(result->mutex_);
        result->addresses_ = std::move(addresses);
        result->ready_ = true;
      }
      const uint64_t one = 1;
      while (::write(result->fd(), &one, sizeof one) < 0 && errno == EINTR) {}
    }).detach();
  } catch (const std::system_error&) { return {}; }
  return result;
}

Resolver::Addresses Resolver::lookup(const std::string& host, const std::string& port, std::size_t maximum) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* found = nullptr;
  if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &found) != 0) return {};
  const std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> owner(found, ::freeaddrinfo);
  Addresses addresses;
  for (const auto* a = found; a && addresses.size() < maximum; a = a->ai_next) {
    if (!a->ai_addr || a->ai_addrlen > sizeof(sockaddr_storage)) continue;
    SocketAddress address;
    address.family = a->ai_family;
    address.size = a->ai_addrlen;
    std::memcpy(&address.addr, a->ai_addr, a->ai_addrlen);
    addresses.push_back(address);
  }
  return addresses;
}

bool Resolver::take(Addresses& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ready_) return false;
  ready_ = false;
  out = std::move(addresses_);
  uint64_t count;
  while (::read(fd(), &count, sizeof count) < 0 && errno == EINTR) {}
  return true;
}

int connectionError(int fd) {
  int error = 0;
  socklen_t size = sizeof error;
  return ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 ? error : errno;
}

ConnectResult connectNext(const SocketAddress* addresses, std::size_t count, std::size_t& next) {
  ConnectResult result;
  while (next < count) {
    const auto& address = addresses[next++];
    if (address.size > sizeof address.addr) { result.error = EINVAL; continue; }
    result.fd.reset(::socket(address.family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (!result.fd.valid()) { result.error = errno; continue; }
    const int connected = ::connect(result.fd.get(), reinterpret_cast<const sockaddr*>(&address.addr), address.size);
    if (connected == 0 || errno == EINPROGRESS) {
      result.state = connected == 0 ? ConnectState::Connected : ConnectState::Pending;
      return result;
    }
    result.error = errno;
    result.fd.reset();
  }
  return result;
}
}
