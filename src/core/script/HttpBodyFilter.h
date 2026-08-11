#pragma once

#include <cstddef>
#include <string>

namespace awtrix::script {

constexpr std::size_t kDefaultHttpKeep = 256;

// Largest single step the kept bytes grow by. Doubling would ask for a block twice the size of
// what is already held, which a device with room for the answer itself may not have in one piece.
constexpr std::size_t kBodyGrowStepBytes = 4 * 1024;

// Keeps only the part of a response a script wants, as the bytes arrive, so a large body is
// never held whole. Without a needle that is the first `cap` bytes; with one it is the `keep`
// bytes starting at the match, which reaches a field however far into the document it sits.
class HttpBodyFilter {
 public:
  void begin(const std::string& find, std::size_t keep, std::size_t cap);

  // Call with each chunk in order. Ignores everything once done() -- the caller may keep
  // feeding, or stop early and drop the rest of the connection.
  void feed(const char* data, std::size_t len);

  bool done() const { return want_ == 0; }

  bool matched() const { return !searching_; }

  // True when the answer stopped fitting part way through. The kept bytes are incomplete, so a
  // caller reports a failed request rather than handing the script a half answer.
  bool outOfRoom() const { return outOfRoom_; }

  std::string& body() { return body_; }

 private:
  // Makes room for `take` more bytes, or gives up. Never reserves past the most the answer can
  // still come to, so a short one costs what it is rather than what was asked for.
  bool makeRoomFor(std::size_t take);

  std::string find_;
  // Last few bytes of the previous chunk, carried over so a needle split across a chunk
  // boundary is still found.
  std::string tail_;
  std::string body_;
  // Bytes still wanted; 0 means done.
  std::size_t want_ = 0;
  bool searching_ = false;
  bool outOfRoom_ = false;
};

}
