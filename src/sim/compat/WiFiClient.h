#pragma once


#include "Client.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

#include <cstdint>
#include <cstdio>

namespace awtrix {
namespace sim {

#if defined(_WIN32)
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

inline bool netStartup() {
#if defined(_WIN32)
  static const bool ok = [] {
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
  }();
  return ok;
#else
  return true;
#endif
}

// Short on purpose: the simulator has one thread for everything, so a dead broker must not stall
// the render loop for more than a couple of frames.
constexpr long kConnectTimeoutMs = 400;

}
}

// A real non-blocking TCP socket dressed up as Arduino's WiFiClient, so the device's MQTT stack
// links against it without a single change.
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
    if (!awtrix::sim::netStartup() || !host) return 0;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[8];
    std::snprintf(portStr, sizeof(portStr), "%u", static_cast<unsigned>(port));
    addrinfo* res = nullptr;
    if (::getaddrinfo(host, portStr, &hints, &res) != 0 || !res) return 0;

    int ok = 0;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
      awtrix::sim::socket_t s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      if (s == awtrix::sim::kInvalidSocket) continue;
      setNonBlocking(s);
      if (tryConnect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen))) {
        setNoDelay(s);
        sock_ = s;
        connected_ = true;
        ok = 1;
        break;
      }
      closeRaw(s);
    }
    ::freeaddrinfo(res);
    return ok;
  }

  size_t write(uint8_t b) override { return write(&b, 1); }

  size_t write(const uint8_t* buf, size_t size) override {
    if (sock_ == awtrix::sim::kInvalidSocket || !buf) return 0;
    size_t sent = 0;
    while (sent < size) {
      const int n = ::send(sock_, reinterpret_cast<const char*>(buf + sent),
                           static_cast<int>(size - sent), kSendFlags);
      if (n > 0) {
        sent += static_cast<size_t>(n);
        continue;
      }
      if (n < 0 && wouldBlock() && waitWritable()) continue;
      connected_ = false;
      break;
    }
    return sent;
  }

  int available() override {
    if (sock_ == awtrix::sim::kInvalidSocket) return 0;
#if defined(_WIN32)
    u_long n = 0;
    if (::ioctlsocket(sock_, FIONREAD, &n) != 0) return 0;
    return static_cast<int>(n);
#else
    int n = 0;
    if (::ioctl(sock_, FIONREAD, &n) != 0) return 0;
    return n;
#endif
  }

  int read() override {
    uint8_t b = 0;
    return read(&b, 1) == 1 ? static_cast<int>(b) : -1;
  }

  // Arduino's convention, and the socket is non-blocking: 0 means nothing has arrived yet, -1 means
  // the peer closed or the socket broke.
  int read(uint8_t* buf, size_t size) override {
    if (sock_ == awtrix::sim::kInvalidSocket || !buf || size == 0) return -1;
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
    if (sock_ == awtrix::sim::kInvalidSocket) return -1;
    uint8_t b = 0;
    const int n = ::recv(sock_, reinterpret_cast<char*>(&b), 1, MSG_PEEK);
    return n == 1 ? static_cast<int>(b) : -1;
  }

  void flush() override {}

  void stop() override {
    if (sock_ != awtrix::sim::kInvalidSocket) {
      closeRaw(sock_);
      sock_ = awtrix::sim::kInvalidSocket;
    }
    connected_ = false;
  }

  // A one-byte MSG_PEEK is the only way to notice the peer vanished without eating buffered data.
  uint8_t connected() override {
    if (sock_ == awtrix::sim::kInvalidSocket) return 0;
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

  operator bool() override { return sock_ != awtrix::sim::kInvalidSocket; }

 private:
  // Writing to a socket the broker already closed raises SIGPIPE on POSIX and would kill the
  // process; MSG_NOSIGNAL turns that into a plain error return.
#if defined(_WIN32)
  static constexpr int kSendFlags = 0;
#elif defined(MSG_NOSIGNAL)
  static constexpr int kSendFlags = MSG_NOSIGNAL;
#else
  static constexpr int kSendFlags = 0;
#endif

  static void setNonBlocking(awtrix::sim::socket_t s) {
#if defined(_WIN32)
    u_long m = 1;
    ::ioctlsocket(s, FIONBIO, &m);
#else
    const int fl = ::fcntl(s, F_GETFL, 0);
    ::fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
  }

  static void setNoDelay(awtrix::sim::socket_t s) {
    const int one = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
  }

  static bool wouldBlock() {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN;
#endif
  }

  static bool connectInProgress() {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EINPROGRESS;
#endif
  }

  static bool tryConnect(awtrix::sim::socket_t s, const sockaddr* addr, int addrlen) {
    if (::connect(s, addr, addrlen) == 0) return true;
    if (!connectInProgress()) return false;

    fd_set wf;
    fd_set ef;
    FD_ZERO(&wf);
    FD_ZERO(&ef);
    FD_SET(s, &wf);
    FD_SET(s, &ef);
    timeval tv;
    tv.tv_sec = awtrix::sim::kConnectTimeoutMs / 1000;
    tv.tv_usec = (awtrix::sim::kConnectTimeoutMs % 1000) * 1000;
    const int n = ::select(selectNfds(s), nullptr, &wf, &ef, &tv);
    if (n <= 0 || FD_ISSET(s, &ef)) return false;

    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0) return false;
    return err == 0;
  }

  bool waitWritable() {
    fd_set wf;
    FD_ZERO(&wf);
    FD_SET(sock_, &wf);
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 50 * 1000;
    return ::select(selectNfds(sock_), nullptr, &wf, nullptr, &tv) > 0;
  }

  static int selectNfds(awtrix::sim::socket_t s) {
#if defined(_WIN32)
    (void)s;
    return 0;
#else
    return static_cast<int>(s) + 1;
#endif
  }

  static void closeRaw(awtrix::sim::socket_t s) {
#if defined(_WIN32)
    ::closesocket(s);
#else
    ::close(s);
#endif
  }

  awtrix::sim::socket_t sock_ = awtrix::sim::kInvalidSocket;
  bool connected_ = false;
};
