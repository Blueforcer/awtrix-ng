#include "platform/posix/WorkerThread.h"

#include <utility>

#include "platform/posix/StartThread.h"

namespace awtrix::posix {

WorkerThread::~WorkerThread() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) thread_.join();
}

bool WorkerThread::post(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!thread_.joinable() && !startThread(thread_, [this] { run(); })) return false;
    tasks_.push_back(std::move(task));
  }
  wake_.notify_one();
  return true;
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
