#include "platform/linux/host/OnDemandWorkers.h"

#include <thread>
#include <utility>

namespace awtrix::host_http {

bool OnDemandWorkers::startDetached(std::function<void()> body) {
  try {
    std::thread(std::move(body)).detach();
    return true;
  } catch (...) {
    return false;
  }
}

OnDemandWorkers::OnDemandWorkers(std::size_t keep, std::size_t most, std::chrono::milliseconds idle, StartThread start)
    : keep_(keep), most_(most), idle_(idle), start_(std::move(start)) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (keep_) startOne();
}

OnDemandWorkers::~OnDemandWorkers() { shutdown(); }

// With mutex_ held.
bool OnDemandWorkers::startOne() {
  ++threads_;
  ++free_;
  if (start_([this] { work(); })) return true;
  --threads_;
  --free_;
  return false;
}

bool OnDemandWorkers::enqueue(std::function<void()> job) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (stopping_) return false;
  jobs_.push_back(std::move(job));
  if (jobs_.size() <= free_) {
    lock.unlock();
    work_.notify_one();
    return true;
  }
  if (threads_ < most_ && !startOne() && !threads_) {
    jobs_.pop_back();
    return false;
  }
  return true;
}

void OnDemandWorkers::work() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    work_.wait_for(lock, idle_, [this] { return stopping_ || !jobs_.empty(); });
    if (!jobs_.empty()) {
      std::function<void()> job = std::move(jobs_.front());
      jobs_.pop_front();
      --free_;
      lock.unlock();
      job();
      job = nullptr;
      lock.lock();
      ++free_;
      continue;
    }
    if (stopping_ || threads_ > keep_) break;
  }
#if defined(CPPHTTPLIB_OPENSSL_SUPPORT) && !defined(OPENSSL_IS_BORINGSSL) && !defined(LIBRESSL_VERSION_NUMBER)
  OPENSSL_thread_stop();
#endif
  --free_;
  --threads_;
  ended_.notify_all();
}

void OnDemandWorkers::shutdown() {
  std::unique_lock<std::mutex> lock(mutex_);
  stopping_ = true;
  work_.notify_all();
  ended_.wait(lock, [this] { return !threads_; });
}

std::size_t OnDemandWorkers::threads() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return threads_;
}

}
