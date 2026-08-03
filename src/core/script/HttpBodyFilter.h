#pragma once

#include <cstddef>
#include <string>

namespace awtrix::script {

constexpr std::size_t kMaxHttpFind = 64;

constexpr std::size_t kDefaultHttpKeep = 256;

// Keeps only the part of a response a script wants, as the bytes arrive, so a large body is
// never held whole. A needle takes `keep` bytes from it, reaching a field past the cap.
class HttpBodyFilter {
 public:
  void begin(const std::string& find, std::size_t keep, std::size_t cap);

  // Call with each chunk in order. Ignores everything once done() -- the caller may keep
  // feeding, or stop early and drop the rest of the connection.
  void feed(const char* data, std::size_t len);

  bool done() const { return want_ == 0; }

  bool matched() const { return !searching_; }

  std::string& body() { return body_; }

 private:
  std::string find_;
  // Last few bytes of the previous chunk, carried over so a needle split across a chunk
  // boundary is still found.
  std::string tail_;
  std::string body_;
  // Bytes still wanted; 0 means done.
  std::size_t want_ = 0;
  bool searching_ = false;
};

}
