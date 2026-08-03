#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>

#include "media/MediaHeap.h"

namespace awtrix {
namespace media {

// Growable byte-ish buffer for media payloads: prefers PSRAM, never throws, and reports
// allocation failure through a bool because running out of memory here is routine, not fatal.
template <typename T>
class PodBuffer {
  static_assert(std::is_trivially_copyable<T>::value, "PodBuffer is for POD-like elements only");

 public:
  // Shrinking only moves the size; capacity is kept so the decoders can size a buffer to the
  // worst case and then trim it to what they actually produced without a second allocation.
  bool resize(std::size_t n) {
    if (n <= cap_) {
      size_ = n;
      return true;
    }
    std::size_t newCap = cap_ ? cap_ * 2 : 16;
    if (newCap < n) newCap = n;
    T* p = static_cast<T*>(heap::acquire(newCap * sizeof(T)));
    if (!p) return false;
    if (size_) std::memcpy(p, data_.get(), size_ * sizeof(T));
    data_.reset(p);
    cap_ = newCap;
    size_ = n;
    return true;
  }

  void shrinkToFit() {
    if (cap_ == size_) return;
    if (size_ == 0) {
      clear();
      return;
    }
    // Failing to re-allocate is harmless here: keep the oversized block and carry on.
    T* p = static_cast<T*>(heap::acquire(size_ * sizeof(T)));
    if (!p) return;
    std::memcpy(p, data_.get(), size_ * sizeof(T));
    data_.reset(p);
    cap_ = size_;
  }

  void clear() {
    data_.reset();
    cap_ = 0;
    size_ = 0;
  }

  T* data() { return data_.get(); }
  const T* data() const { return data_.get(); }
  std::size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  T& operator[](std::size_t i) { return data_[i]; }
  const T& operator[](std::size_t i) const { return data_[i]; }

 private:
  struct Release {
    void operator()(T* p) const { heap::release(p); }
  };
  std::unique_ptr<T[], Release> data_;
  std::size_t size_ = 0;
  std::size_t cap_ = 0;
};

}
}
