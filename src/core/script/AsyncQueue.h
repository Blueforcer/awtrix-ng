#pragma once

#include <cstddef>
#include <deque>
#include <mutex>

namespace awtrix::script {

// A toolchain without threads (arm-none-eabi newlib, the RP2040 build) has no std::mutex. There
// every producer runs on the main loop too - MQTT is delivered from its tick - so nothing races.
#if defined(__GLIBCXX__) && !defined(_GLIBCXX_HAS_GTHREADS)
struct AsyncQueueLock {
  void lock() {}
  void unlock() {}
};
#else
using AsyncQueueLock = std::mutex;
#endif

// Hands results from the network task to the script host on the main loop. Bounded and
// lossy by design: the producer must never block on a script that is slow to drain.
template <typename T, std::size_t Cap>
class AsyncQueue {
 public:
  // Overflow drops the OLDEST entry, so a backlog costs the stalest result, not the newest.
  void push(T v) {
    std::lock_guard<AsyncQueueLock> l(m_);
    if (q_.size() >= Cap) q_.pop_front();
    q_.push_back(std::move(v));
  }

  bool pop(T& out) {
    std::lock_guard<AsyncQueueLock> l(m_);
    if (q_.empty()) return false;
    out = std::move(q_.front());
    q_.pop_front();
    return true;
  }

 private:
  AsyncQueueLock m_;
  std::deque<T> q_;
};

}
