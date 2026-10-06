#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace awtrix::audio {

// Compressed stream bytes stay in a ring until the decoder consumes them. A frame crossing the
// end is copied into a one-frame staging area in the same allocation, never by compacting the
// ring. Sync scanning keeps the final three bytes when it cannot yet see a complete header.
class StreamInputBuffer {
 public:
  // The largest supported Layer III frame, padding included.
  static constexpr std::size_t kMaxFrameBytes = 1441;

  struct View {
    const uint8_t* data;
    std::size_t size;
  };

  explicit StreamInputBuffer(std::size_t capacity)
      : storage_(capacity ? new uint8_t[capacity + kMaxFrameBytes] : nullptr),
        capacity_(capacity) {}

  std::size_t size() const { return size_; }
  std::size_t room() const { return capacity_ - size_; }

  bool push(const uint8_t* data, std::size_t bytes) {
    if (bytes > room() || (bytes && !data)) return false;
    if (!bytes) return true;
    const std::size_t tail = (head_ + size_) % capacity_;
    const std::size_t first = std::min(bytes, capacity_ - tail);
    std::memcpy(storage_.get() + tail, data, first);
    if (first < bytes) std::memcpy(storage_.get(), data + first, bytes - first);
    size_ += bytes;
    return true;
  }

  bool consume(std::size_t bytes) {
    if (bytes > size_) return false;
    if (bytes) head_ = (head_ + bytes) % capacity_;
    size_ -= bytes;
    return true;
  }

  void clear() {
    head_ = 0;
    size_ = 0;
  }

  // A long contiguous tail contains a complete maximum-size frame at its start. If sync instead
  // finds a partial frame later in that span, its bytesConsumed advances the ring to that header;
  // the next call then bridges the wrap. NeedMoreData never consumes an incomplete header/frame.
  View decoderView() {
    if (!size_) return {nullptr, 0};
    const std::size_t first = std::min(size_, capacity_ - head_);
    if (first == size_ || first >= kMaxFrameBytes)
      return {storage_.get() + head_, first};
    const std::size_t bytes = std::min(size_, kMaxFrameBytes);
    uint8_t* staged = storage_.get() + capacity_;
    std::memcpy(staged, storage_.get() + head_, first);
    std::memcpy(staged + first, storage_.get(), bytes - first);
    return {staged, bytes};
  }

 private:
  std::unique_ptr<uint8_t[]> storage_;
  std::size_t capacity_;
  std::size_t head_ = 0;
  std::size_t size_ = 0;
};

}
