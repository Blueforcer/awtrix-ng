#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <utility>

#include "platform/posix/Executor.h"

namespace awtrix::test {

class ManualExecutor final : public posix::Executor {
 public:
  bool post(std::function<void()> task) override {
    if (refusing) return false;
    tasks_.push_back(std::move(task));
    return true;
  }
  std::size_t pending() const { return tasks_.size(); }
  bool refusing = false;
  void run() {
    while (!tasks_.empty()) {
      std::function<void()> task = std::move(tasks_.front());
      tasks_.pop_front();
      task();
    }
  }

 private:
  std::deque<std::function<void()>> tasks_;
};

}
