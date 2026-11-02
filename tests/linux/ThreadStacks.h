#pragma once

#include <pthread.h>

#include <cstddef>

namespace awtrix::test {

class ThreadStacks {
 public:
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

constexpr std::size_t kSmallStackBytes = std::size_t{512} * 1024;
constexpr std::size_t kImpossibleStackBytes = std::size_t{1} << 46;

}
