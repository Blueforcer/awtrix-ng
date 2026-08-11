#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

namespace awtrix {

// Fixed buffer a request body is streamed into, so no growing std::string fragments the heap
// mid-upload. Overflow is remembered rather than reported, and answered after the body is drained.
// The buffer is allocated once, at `capacity`, and never moves: WebServer reads exactly the
// declared Content-Length and nothing without one, so there is nothing to discover part-way
// through that a bigger buffer could rescue.
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

  // cap is the limit for this one request and can only lower what the buffer already holds room
  // for, never raise it.
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

// RAW_START's allocation policy for a raw script-source upload against a live ceiling: a declared
// length within it gets one fixed allocation of exactly that size, so a short script pays for a
// short script. A length past the ceiling gets no allocation, so an upload that could never fit
// never touches the heap; it is left Overflow, which is what takeBody() answers 507 on.
//
// No length at all is not a refusal for room -- WebServer then reads no body either, so there is
// nothing here to turn away. The arena is left Idle and the request goes on to be answered as
// the empty body it is.
inline void openSourceArena(BodyArena& arena, int declaredContentLength, std::size_t ceiling) {
  // Released before either decision, since an arena reused on the same connection may still be
  // holding the previous request's buffer, and both branches below must start from nothing.
  arena.release();
  if (declaredContentLength <= 0) return;
  if (static_cast<std::size_t>(declaredContentLength) <= ceiling)
    arena.init(static_cast<std::size_t>(declaredContentLength));
  arena.open(ceiling);
}

// A raw-source body's ceiling depends on how it arrived. One the arena took charge of had its
// ceiling measured and stored at RAW_START (arenaCeiling), and that reading is the one its
// refusal has to quote -- re-measuring a heap the upload has since spent would name a figure
// that was never the one it was judged against. A body the arena never took charge of has no
// stored reading belonging to it, and a leftover one from an earlier request would be worse
// than none, so it is measured fresh.
inline std::size_t sourceCeilingFor(bool wentThroughArena, std::size_t arenaCeiling,
                                    std::size_t liveCeiling) {
  return wentThroughArena ? arenaCeiling : liveCeiling;
}

// One wording for every way a source upload can be turned away for room. The author cannot act
// on which of them it was, only on the figure, and one message is one row in the reference.
inline std::string sourceTooLargeMessage(std::size_t ceiling) {
  return "script source exceeds the " + std::to_string(ceiling) + " bytes free to receive it";
}

}
