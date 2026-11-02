#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "platform/posix/Executor.h"

namespace awtrix::posix {

class WorkerThread final : public Executor {
 public:
  WorkerThread() = default;
  ~WorkerThread() override;
  WorkerThread(const WorkerThread&) = delete;
  WorkerThread& operator=(const WorkerThread&) = delete;

  bool post(std::function<void()> task) override;

 private:
  void run();

  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> tasks_;
  bool quit_ = false;
  std::thread thread_;
};

}
