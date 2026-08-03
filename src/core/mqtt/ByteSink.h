#pragma once

#include <cstddef>
#include <cstring>
#include <string>

namespace awtrix {
namespace ha {

class IByteSink {
 public:
  virtual ~IByteSink() = default;
  virtual void write(const char* data, std::size_t len) = 0;

  void put(char c) { write(&c, 1); }
  void put(const char* s) { write(s, std::strlen(s)); }
  void put(const std::string& s) { write(s.data(), s.size()); }
};

// Sizing pass. PubSubClient wants the exact payload length before the first byte goes out, so the
// discovery document is emitted once into this and then again into the real sink.
class CountingSink : public IByteSink {
 public:
  void write(const char*, std::size_t len) override { n += len; }

  std::size_t n = 0;
};

class StringSink : public IByteSink {
 public:
  void write(const char* data, std::size_t len) override { str.append(data, len); }

  std::string str;
};

}
}
