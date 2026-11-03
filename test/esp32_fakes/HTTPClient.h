#pragma once

#include <Arduino.h>
#include <WiFiClient.h>

#include <atomic>
#include <string>

constexpr int HTTPC_STRICT_FOLLOW_REDIRECTS = 1;

namespace fake {
inline std::atomic<int> httpFetches{0};
inline int httpStatus = 200;
inline std::string httpBody;
}

class HTTPClient {
 public:
  void setConnectTimeout(int) {}
  void setTimeout(int) {}
  void setReuse(bool) {}
  void setFollowRedirects(int) {}
  bool begin(WiFiClient&, const char*) { return true; }
  void addHeader(const char*, const char*) {}
  void setUserAgent(const char*) {}
  int sendRequest(const char*, uint8_t*, size_t) {
    ++fake::httpFetches;
    return fake::httpStatus;
  }
  int writeToStream(Stream* stream) {
    stream->write(reinterpret_cast<const uint8_t*>(fake::httpBody.data()), fake::httpBody.size());
    return static_cast<int>(fake::httpBody.size());
  }
  void end() {}
};
