#pragma once

#include "Client.h"
#include "platform/posix/Resolver.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>

// Non-blocking Linux TCP client implementing the interface required by PubSubClient.
class WiFiClient : public Client {
 public:
  WiFiClient() = default;
  ~WiFiClient() override { stop(); }

  WiFiClient(const WiFiClient&) = delete;
  WiFiClient& operator=(const WiFiClient&) = delete;

  int connect(IPAddress ip, uint16_t port) override {
    return connect(ip.toString().c_str(), port);
  }

  int connect(const char* host, uint16_t port) override {
    stop();
    if (!host) return 0;
    const auto addresses = awtrix::posix::Resolver::lookup(host, std::to_string(port));
    for (const auto& address : addresses) {
      if (address.family != AF_INET) continue;
      std::size_t next = 0;
      auto connection = awtrix::posix::connectNext(&address, 1, next);
      if (!connection.fd.valid()) continue;
      const int fd = connection.fd.get();
      if (connection.state != awtrix::posix::ConnectState::Connected &&
          (!waitWritable(fd, 400) || awtrix::posix::connectionError(fd) != 0)) continue;
      const int one = 1;
      ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
      sock_ = connection.fd.release();
      connected_ = true;
      return 1;
    }
    return 0;
  }

  size_t write(uint8_t b) override { return write(&b, 1); }

  size_t write(const uint8_t* buf, size_t size) override {
    if (sock_ == -1 || !buf) return 0;
    size_t sent = 0;
    while (sent < size) {
      const int n = ::send(sock_, reinterpret_cast<const char*>(buf + sent),
                           static_cast<int>(size - sent), MSG_NOSIGNAL);
      if (n > 0) {
        sent += static_cast<size_t>(n);
        continue;
      }
      if (n < 0 && wouldBlock() && waitWritable(sock_, 50)) continue;
      connected_ = false;
      break;
    }
    return sent;
  }

  int available() override {
    if (sock_ == -1) return 0;
    int n = 0;
    if (::ioctl(sock_, FIONREAD, &n) != 0) return 0;
    return n;
  }

  int read() override {
    uint8_t b = 0;
    return read(&b, 1) == 1 ? static_cast<int>(b) : -1;
  }

  // Arduino's convention, and the socket is non-blocking: 0 means nothing has arrived yet, -1 means
  // the peer closed or the socket broke.
  int read(uint8_t* buf, size_t size) override {
    if (sock_ == -1 || !buf || size == 0) return -1;
    const int n = ::recv(sock_, reinterpret_cast<char*>(buf), static_cast<int>(size), 0);
    if (n > 0) return n;
    if (n == 0) {
      connected_ = false;
      return -1;
    }
    if (wouldBlock()) return 0;
    connected_ = false;
    return -1;
  }

  int peek() override {
    if (sock_ == -1) return -1;
    uint8_t b = 0;
    const int n = ::recv(sock_, reinterpret_cast<char*>(&b), 1, MSG_PEEK);
    return n == 1 ? static_cast<int>(b) : -1;
  }

  void flush() override {}

  void stop() override {
    if (sock_ != -1) {
      ::close(sock_);
      sock_ = -1;
    }
    connected_ = false;
  }

  // A one-byte MSG_PEEK is the only way to notice the peer vanished without eating buffered data.
  uint8_t connected() override {
    if (sock_ == -1) return 0;
    if (connected_) {
      uint8_t b = 0;
      const int n = ::recv(sock_, reinterpret_cast<char*>(&b), 1, MSG_PEEK);
      if (n == 0)
        connected_ = false;
      else if (n < 0 && !wouldBlock())
        connected_ = false;
    }
    return connected_ ? 1 : 0;
  }

  operator bool() override { return sock_ != -1; }

 private:
  static bool wouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN; }

  // The loop thread waits at most 400 ms for connect and 50 ms for write progress.
  static bool waitWritable(int fd, int timeoutMs) {
    pollfd descriptor{fd, POLLOUT, 0};
    return ::poll(&descriptor, 1, timeoutMs) > 0 && (descriptor.revents & POLLOUT);
  }

  int sock_ = -1;
  bool connected_ = false;
};
