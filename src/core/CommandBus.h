#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <utility>

#include "core/Command.h"

namespace awtrix {

// Fixed-size ring for commands that are not urgent enough to run inline. It is not synchronised:
// everything that pushes runs from the main loop, and CoreEngine::tick drains it there too. The
// slots appear with the first command, and without the memory for them that command is refused the
// way a full ring refuses one.
class CommandBus {
 public:
  explicit CommandBus(std::size_t capacity) : capacity_(capacity) {}

  std::size_t capacity() const { return capacity_; }
  std::size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  bool full() const { return count_ == capacity_; }

  bool push(Command c) {
    if (full()) return false;
    if (!buf_) buf_.reset(new (std::nothrow) Command[capacity_]);
    if (!buf_) return false;
    buf_[head_] = std::move(c);
    head_ = (head_ + 1) % capacity_;
    ++count_;
    return true;
  }

  bool pop(Command& out) {
    if (empty()) return false;
    // Move-constructs a temporary; the previous output buffers are freed and the slot is empty.
    out = Command(std::move(buf_[tail_]));
    buf_[tail_] = Command{};
    tail_ = (tail_ + 1) % capacity_;
    --count_;
    return true;
  }

 private:
  std::unique_ptr<Command[]> buf_;
  std::size_t capacity_;
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  std::size_t count_ = 0;
};

}
