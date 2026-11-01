#include "platform/posix/WorkerThread.h"

#include <utility>

namespace awtrix::posix {

WorkerThread::~WorkerThread() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void WorkerThread::post(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push_back(std::move(task));
    if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
  }
  wake_.notify_one();
}

void WorkerThread::run() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    wake_.wait(lock, [this] { return quit_ || !tasks_.empty(); });
    if (quit_) return;
    std::function<void()> task = std::move(tasks_.front());
    tasks_.pop_front();
    lock.unlock();
    task();
    lock.lock();
  }
}

}
