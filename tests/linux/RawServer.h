#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../support.h"

namespace awtrix::test {

class RawServer {
 public:
  explicit RawServer(std::function<void(int peer)> serve) : serve_(std::move(serve)) {
    constexpr int kReceiveBufferBytes = 64 * 1024;
    listener_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof address;
    require(listener_ >= 0 &&
                ::setsockopt(listener_, SOL_SOCKET, SO_RCVBUF, &kReceiveBufferBytes, sizeof kReceiveBufferBytes) == 0 &&
                ::bind(listener_, reinterpret_cast<sockaddr*>(&address), size) == 0 && ::listen(listener_, 16) == 0 &&
                ::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) == 0,
            "a raw loopback server");
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this] { run(); });
  }
  ~RawServer() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      if (peer_ >= 0) ::shutdown(peer_, SHUT_RDWR);
    }
    ::shutdown(listener_, SHUT_RDWR);
    thread_.join();
    ::close(listener_);
  }
  RawServer(const RawServer&) = delete;
  RawServer& operator=(const RawServer&) = delete;

  std::string url(const char* scheme = "http") const {
    return std::string(scheme) + "://127.0.0.1:" + std::to_string(port_) + "/";
  }

 private:
  void run() {
    for (;;) {
      const int peer = ::accept(listener_, nullptr, nullptr);
      if (peer < 0) return;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        peer_ = peer;
        if (stopping_) ::shutdown(peer, SHUT_RDWR);
      }
      serve_(peer);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        peer_ = -1;
      }
      ::close(peer);
    }
  }

  std::function<void(int peer)> serve_;
  int listener_ = -1;
  int port_ = 0;
  std::mutex mutex_;
  int peer_ = -1;
  bool stopping_ = false;
  std::thread thread_;
};

inline void readRequest(int peer) {
  std::string seen;
  char block[4096];
  while (seen.find("\r\n\r\n") == std::string::npos) {
    const ssize_t count = ::recv(peer, block, sizeof block, 0);
    if (count <= 0) return;
    seen.append(block, static_cast<std::size_t>(count));
  }
}

inline void waitForClose(int peer) {
  char block[4096];
  while (::recv(peer, block, sizeof block, 0) > 0) {
  }
}

inline void sendText(int peer, const std::string& text) { ::send(peer, text.data(), text.size(), MSG_NOSIGNAL); }

struct HostileAnswer {
  const char* name;
  std::string head;
  int status;
};

inline std::vector<HostileAnswer> hostileAnswers() {
  const std::string padding(8000, 'a');
  const std::string redirect = "HTTP/1.1 302 Found\r\nContent-Length: 0\r\nLocation: ";
  return {{"a reason of 8 KB", "HTTP/1.1 200 " + padding + "\r\nContent-Length: 0\r\n\r\n", 200},
          {"a Location of 8 KB", redirect + "/" + padding + "\r\n\r\n", 0},
          {"a Location with a huge port", redirect + "http://127.0.0.1:99999999999/x\r\n\r\n", 0}};
}

}
