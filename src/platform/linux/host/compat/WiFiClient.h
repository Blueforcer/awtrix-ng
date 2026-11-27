#pragma once

#include "Client.h"
#include "platform/host/Resolver.h"
#include "platform/host/Socket.h"

#include <cstdint>
#include <string>

// Non-blocking host TCP client implementing the interface required by PubSubClient.
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
    const auto addresses = awtrix::os::Resolver::lookup(host, std::to_string(port));
    for (const auto& address : addresses) {
      if (address.family != AF_INET) continue;
      std::size_t next = 0;
      auto connection = awtrix::os::connectNext(&address, 1, next);
      if (!connection.socket.valid()) continue;
      const awtrix::os::Socket socket = connection.socket.get();
      if (connection.state != awtrix::os::ConnectState::Connected &&
          (!awtrix::os::waitWritable(socket, 400) || awtrix::os::pendingError(socket) != 0)) continue;
      awtrix::os::noDelay(socket);
      sock_ = connection.socket.release();
      connected_ = true;
      return 1;
    }
    return 0;
  }

  size_t write(uint8_t b) override { return write(&b, 1); }

  size_t write(const uint8_t* buf, size_t size) override {
    if (sock_ == awtrix::os::kNoSocket || !buf) return 0;
    size_t sent = 0;
    while (sent < size) {
      const long n = awtrix::os::sendBytes(sock_, buf + sent, size - sent);
      if (n > 0) {
        sent += static_cast<size_t>(n);
        continue;
      }
      if (n < 0 && wouldBlock() && awtrix::os::waitWritable(sock_, 50)) continue;
      connected_ = false;
      break;
    }
    return sent;
  }

  int available() override {
    if (sock_ == awtrix::os::kNoSocket) return 0;
    return awtrix::os::pendingBytes(sock_);
  }

  int read() override {
    uint8_t b = 0;
    return read(&b, 1) == 1 ? static_cast<int>(b) : -1;
  }

  // Arduino's convention, and the socket is non-blocking: 0 means nothing has arrived yet, -1 means
  // the peer closed or the socket broke.
  int read(uint8_t* buf, size_t size) override {
    if (sock_ == awtrix::os::kNoSocket || !buf || size == 0) return -1;
    const long n = awtrix::os::receiveBytes(sock_, buf, size);
    if (n > 0) return static_cast<int>(n);
    if (n == 0) {
      connected_ = false;
      return -1;
    }
    if (wouldBlock()) return 0;
    connected_ = false;
    return -1;
  }

  int peek() override {
    if (sock_ == awtrix::os::kNoSocket) return -1;
    uint8_t b = 0;
    const long n = awtrix::os::receiveBytes(sock_, &b, 1, true);
    return n == 1 ? static_cast<int>(b) : -1;
  }

  void flush() override {}

  void stop() override {
    if (sock_ != awtrix::os::kNoSocket) {
      awtrix::os::closeSocket(sock_);
      sock_ = awtrix::os::kNoSocket;
    }
    connected_ = false;
  }

  // A one-byte peek is the only way to notice the peer vanished without eating buffered data.
  uint8_t connected() override {
    if (sock_ == awtrix::os::kNoSocket) return 0;
    if (connected_) {
      uint8_t b = 0;
      const long n = awtrix::os::receiveBytes(sock_, &b, 1, true);
      if (n == 0)
        connected_ = false;
      else if (n < 0 && !wouldBlock())
        connected_ = false;
    }
    return connected_ ? 1 : 0;
  }

  operator bool() override { return sock_ != awtrix::os::kNoSocket; }

 private:
  static bool wouldBlock() { return awtrix::os::wouldBlock(awtrix::os::socketError()); }

  awtrix::os::Socket sock_ = awtrix::os::kNoSocket;
  bool connected_ = false;
};
