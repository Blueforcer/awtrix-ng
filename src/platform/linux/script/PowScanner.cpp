#include "platform/linux/script/PowScanner.h"

#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <system_error>

#include "platform/linux/script/Sha256d.h"

namespace awtrix::linux_script {
namespace {

bool lower(const uint8_t a[32], const uint8_t b[32]) {
  for (int i = 31; i >= 0; --i)
    if (a[i] != b[i]) return a[i] < b[i];
  return false;
}

// The top 32 bits of the number lower() compares, as Sha256d::top() yields them.
uint32_t topOf(const uint8_t hash[32]) {
  return static_cast<uint32_t>(hash[31]) << 24 | static_cast<uint32_t>(hash[30]) << 16 |
         static_cast<uint32_t>(hash[29]) << 8 | hash[28];
}

}

void blockHash(const uint8_t header[80], uint8_t out[32]) {
  const uint32_t nonce = static_cast<uint32_t>(header[76]) | static_cast<uint32_t>(header[77]) << 8 |
                         static_cast<uint32_t>(header[78]) << 16 | static_cast<uint32_t>(header[79]) << 24;
  Sha256d(header).hash(nonce, out);
}

bool meets(const uint8_t hash[32], const uint8_t target[32]) { return !lower(target, hash); }

Hash targetOf(double difficulty) {
  Hash t{};
  long double v = difficulty > 0 ? 65535.0L / difficulty : INFINITY;
  if (!(v < std::ldexp(1.0L, 48))) {
    t.fill(0xff);
    return t;
  }
  for (int i = 31; i >= 0; --i) {
    const long double place = std::ldexp(1.0L, 8 * i - 208);
    const long double b = std::min<long double>(255.0L, std::floor(v / place));
    t[i] = static_cast<uint8_t>(b);
    v -= b * place;
  }
  return t;
}

double difficultyOf(const uint8_t hash[32]) {
  long double v = 0;
  for (int i = 31; i >= 0; --i) v = v * 256.0L + hash[i];
  if (v <= 0) return INFINITY;
  return static_cast<double>(65535.0L * std::ldexp(1.0L, 208) / v);
}

bool PowScanner::start(const uint8_t header[80], const uint8_t target[32], uint32_t first, uint32_t last,
                       unsigned threads) {
  stop();
  std::memcpy(header_, header, sizeof header_);
  std::memcpy(target_, target, sizeof target_);
  last_ = last;
  next_.store(first);
  stop_.store(false);
  if (first > last) {
    push({true, 0, {}});
    return true;
  }
  const unsigned limit = std::max(1u, std::thread::hardware_concurrency());
  const unsigned n = threads == 0 ? limit : std::min(threads, limit);
  active_.store(n);
  ready_ = 0;
  schedulingFailed_ = false;
  try {
    workers_.reserve(n);
    for (unsigned i = 0; i < n; ++i) workers_.emplace_back([this] { work(); });
  } catch (const std::system_error&) {
    stop();
    return false;
  }
  {
    std::unique_lock<std::mutex> lock(mutex_);
    readyCv_.wait(lock, [&] { return ready_ == n; });
    if (!schedulingFailed_) return true;
  }
  stop();
  return false;
}

void PowScanner::stop() {
  stop_.store(true);
  for (auto& w : workers_) w.join();
  workers_.clear();
  active_.store(0);
  std::lock_guard<std::mutex> lock(mutex_);
  events_.clear();
}

bool PowScanner::pop(PowEvent& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (events_.empty()) return false;
  out = events_.front();
  events_.pop_front();
  return true;
}

Hash PowScanner::best() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return best_;
}

void PowScanner::resetBest() {
  std::lock_guard<std::mutex> lock(mutex_);
  best_.fill(0xff);
}

void PowScanner::push(const PowEvent& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!event.done && events_.size() >= kQueue) {
    dropped_.fetch_add(1);
    return;
  }
  events_.push_back(event);
}

void PowScanner::work() {
  sched_param idle{};
  const int scheduled = pthread_setschedparam(pthread_self(), SCHED_IDLE, &idle);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    schedulingFailed_ = schedulingFailed_ || scheduled != 0;
    if (scheduled != 0) stop_.store(true);
    ++ready_;
  }
  readyCv_.notify_one();
  const Sha256d kernel(header_);
  const uint32_t targetTop = topOf(target_);
  Hash localBest;
  localBest.fill(0xff);
  uint32_t bound = 0xffffffffu;
  uint8_t hash[32];
  while (!stop_.load(std::memory_order_relaxed)) {
    const uint64_t begin = next_.fetch_add(kBatch);
    if (begin > last_) break;
    const uint64_t end = std::min<uint64_t>(begin + kBatch - 1, last_);
    bool improved = false;
    uint64_t count = 0;
    for (uint64_t n = begin; n <= end; ++n) {
      if ((count & 255u) == 0 && stop_.load(std::memory_order_relaxed)) break;
      ++count;
      if (kernel.top(static_cast<uint32_t>(n)) > bound) continue;
      kernel.hash(static_cast<uint32_t>(n), hash);
      if (lower(hash, localBest.data())) {
        std::memcpy(localBest.data(), hash, 32);
        bound = std::max(targetTop, topOf(hash));
        improved = true;
      }
      if (meets(hash, target_)) {
        PowEvent e;
        e.nonce = static_cast<uint32_t>(n);
        std::memcpy(e.hash.data(), hash, 32);
        push(e);
      }
    }
    hashes_.fetch_add(count);
    if (improved) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (lower(localBest.data(), best_.data())) best_ = localBest;
    }
  }
  if (active_.fetch_sub(1) == 1 && !stop_.load()) push({true, 0, {}});
}

}
