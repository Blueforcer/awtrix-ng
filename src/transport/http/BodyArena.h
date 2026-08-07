#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <string_view>

namespace awtrix {

// How much to allocate for a body whose limit is cap. A declared Content-Length shrinks the
// allocation to what is actually coming; an absent or non-positive one (chunked upload) has to
// assume the worst. The result is never above cap, so an oversized body still overflows.
inline std::size_t arenaCapacityFor(int contentLength, std::size_t cap) {
  if (contentLength <= 0) return cap;
  const std::size_t declared = static_cast<std::size_t>(contentLength);
  return declared < cap ? declared : cap;
}

// Fixed buffer a request body is streamed into, so no growing std::string fragments the heap
// mid-upload. Overflow is remembered rather than reported, and answered after the body is drained.
class BodyArena {
 public:
  enum class State { Idle, Open, Done, Overflow };

  bool init(std::size_t capacity) {
    buf_.reset(new (std::nothrow) char[capacity]);
    capacity_ = buf_ ? capacity : 0;
    state_ = State::Idle;
    size_ = 0;
    return buf_ != nullptr;
  }

  bool ready() const { return buf_ != nullptr; }
  std::size_t capacity() const { return capacity_; }
  State state() const { return state_; }

  // cap is the limit for this one request and is clamped to the allocated buffer size.
  void open(std::size_t cap) {
    cap_ = cap < capacity_ ? cap : capacity_;
    size_ = 0;
    state_ = buf_ ? State::Open : State::Overflow;
  }

  void append(const void* data, std::size_t len) {
    if (state_ != State::Open) return;
    if (size_ + len > cap_) {
      state_ = State::Overflow;
      size_ = 0;
      return;
    }
    std::memcpy(buf_.get() + size_, data, len);
    size_ += len;
  }

  void finish() {
    if (state_ == State::Open) state_ = State::Done;
  }

  void reset() {
    size_ = 0;
    state_ = State::Idle;
  }

  void release() {
    buf_.reset();
    capacity_ = 0;
    cap_ = 0;
    size_ = 0;
    state_ = State::Idle;
  }

  std::string_view view() const {
    return state_ == State::Done ? std::string_view(buf_.get(), size_) : std::string_view();
  }

 private:
  std::unique_ptr<char[]> buf_;
  std::size_t capacity_ = 0;
  std::size_t cap_ = 0;
  std::size_t size_ = 0;
  State state_ = State::Idle;
};

}
