#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "platform/linux/net/HttpClient.h"

namespace awtrix::net {

// A blocking GET of an http(s) URL into memory, bounded in size and in time from connecting to
// the last byte, redirects included, for one worker thread. Another thread can abort it for good,
// which also fails every later fetch at once.
class HttpGet {
 public:
  struct Limits {
    std::size_t maxBytes = 0;
    int64_t timeoutMs = 15000;
    bool followRedirects = false;
  };
  enum class Failure : uint8_t { None, Network, Status, TooLarge, Aborted };
  struct Result {
    Failure failure = Failure::Network;
    int status = 0;
  };

  Result fetch(const std::string& url, const Limits& limits, std::string& body);
  void abort();

 private:
  std::mutex mutex_;
  SocketInterrupt interrupt_;
  bool aborted_ = false;
};

}
