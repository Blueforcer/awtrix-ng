#pragma once

#include <pthread.h>

#include <cstddef>

namespace awtrix::test {

#if defined(__APPLE__)
class ThreadStacks {
 public:
  static constexpr bool kApplies = false;
  explicit ThreadStacks(std::size_t) {}
  ThreadStacks(const ThreadStacks&) = delete;
  ThreadStacks& operator=(const ThreadStacks&) = delete;
};
#else
class ThreadStacks {
 public:
  static constexpr bool kApplies = true;
  explicit ThreadStacks(std::size_t bytes) {
    pthread_getattr_default_np(&saved_);
    pthread_attr_t wanted;
    pthread_attr_init(&wanted);
    pthread_attr_setstacksize(&wanted, bytes);
    pthread_setattr_default_np(&wanted);
    pthread_attr_destroy(&wanted);
  }
  ~ThreadStacks() {
    pthread_setattr_default_np(&saved_);
    pthread_attr_destroy(&saved_);
  }
  ThreadStacks(const ThreadStacks&) = delete;
  ThreadStacks& operator=(const ThreadStacks&) = delete;

 private:
  pthread_attr_t saved_;
};
#endif

constexpr std::size_t kSmallStackBytes = std::size_t{512} * 1024;
constexpr std::size_t kImpossibleStackBytes = std::size_t{1} << 46;

}
