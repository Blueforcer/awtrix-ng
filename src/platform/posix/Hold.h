#pragma once

#include <functional>
#include <utility>

namespace awtrix::posix {

class Hold {
 public:
  Hold() = default;
  explicit Hold(std::function<void()> release) : release_(std::move(release)) {}
  Hold(Hold&& other) noexcept : release_(std::exchange(other.release_, nullptr)) {}
  Hold& operator=(Hold&& other) noexcept {
    if (this != &other) {
      reset();
      release_ = std::exchange(other.release_, nullptr);
    }
    return *this;
  }
  Hold(const Hold&) = delete;
  Hold& operator=(const Hold&) = delete;
  ~Hold() { reset(); }

  explicit operator bool() const { return static_cast<bool>(release_); }
  void reset() {
    if (const std::function<void()> release = std::exchange(release_, nullptr)) release();
  }

 private:
  std::function<void()> release_;
};

}
