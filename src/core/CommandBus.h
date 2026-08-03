#pragma once

#include <cstddef>
#include <vector>

#include "core/Command.h"

namespace awtrix {

// Fixed-size ring for commands that are not urgent enough to run inline. It is not synchronised:
// everything that pushes runs from the main loop, and CoreEngine::tick drains it there too.
class CommandBus {
 public:
  explicit CommandBus(std::size_t capacity) : buf_(capacity) {}

  std::size_t capacity() const { return buf_.size(); }
  std::size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  bool full() const { return count_ == buf_.size(); }

  bool push(const Command& c) {
    if (full() || buf_.empty()) return false;
    buf_[head_] = c;
    head_ = (head_ + 1) % buf_.size();
    ++count_;
    return true;
  }

  bool pop(Command& out) {
    if (empty()) return false;
    out = buf_[tail_];
    tail_ = (tail_ + 1) % buf_.size();
    --count_;
    return true;
  }

 private:
  std::vector<Command> buf_;
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  std::size_t count_ = 0;
};

}
