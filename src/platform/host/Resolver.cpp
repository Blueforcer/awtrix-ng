#include "platform/host/Resolver.h"

#if !defined(_WIN32)
#include <netdb.h>
#endif

#include <cerrno>
#include <cstring>
#include <system_error>
#include <thread>

namespace awtrix::os {

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
      result->wake_.signal();
    }).detach();
  } catch (const std::system_error&) { return {}; }
  return result;
}

Resolver::Addresses Resolver::lookup(const std::string& host, const std::string& port, std::size_t maximum,
                                     int family) {
  if (!startSockets()) return {};
  addrinfo hints{};
  hints.ai_family = family;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* found = nullptr;
  if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &found) != 0) return {};
  const std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> owner(found, ::freeaddrinfo);
  Addresses addresses;
  for (const auto* a = found; a && addresses.size() < maximum; a = a->ai_next) {
    if (!a->ai_addr || a->ai_addrlen > sizeof(sockaddr_storage)) continue;
    SocketAddress address;
    address.family = a->ai_family;
    address.size = static_cast<socklen_t>(a->ai_addrlen);
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
  wake_.drain();
  return true;
}

ConnectResult connectNext(const SocketAddress* addresses, std::size_t count, std::size_t& next) {
  ConnectResult result;
  while (next < count) {
    const auto& address = addresses[next++];
    if (static_cast<std::size_t>(address.size) > sizeof address.addr) { result.error = EINVAL; continue; }
    result.socket.reset(openStream(address.family));
    if (!result.socket.valid()) { result.error = socketError(); continue; }
    const int error =
        connectSocket(result.socket.get(), reinterpret_cast<const sockaddr*>(&address.addr), address.size);
    if (error == 0 || connecting(error)) {
      result.state = error == 0 ? ConnectState::Connected : ConnectState::Pending;
      return result;
    }
    result.error = error;
    result.socket.reset();
  }
  return result;
}

}
