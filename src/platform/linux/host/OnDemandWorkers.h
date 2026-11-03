#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>

#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::host_http {

// The threads that serve a listener's connections. httplib's ThreadPool starts all of them at once
// and keeps each with the stack its busiest request touched. Here a connection that finds no idle
// thread starts one, up to `most`; a thread without work for `idle` ends and gives its stack back.
// `keep` threads stay, the first of them from the start, so connections are still served when the
// system cannot start another: they wait in line, as they do behind `most` busy threads.
class OnDemandWorkers final : public httplib::TaskQueue {
 public:
  // Runs a body on a new thread that nobody joins; false when no thread could be started.
  using StartThread = std::function<bool(std::function<void()> body)>;
  static bool startDetached(std::function<void()> body);

  OnDemandWorkers(std::size_t keep, std::size_t most, std::chrono::milliseconds idle,
                  StartThread start = startDetached);
  ~OnDemandWorkers() override;

  bool enqueue(std::function<void()> job) override;
  // Connections still in line are served first. Returns when the last thread has left this object.
  void shutdown() override;

  std::size_t threads() const;

 private:
  bool startOne();
  void work();

  const std::size_t keep_, most_;
  const std::chrono::milliseconds idle_;
  const StartThread start_;
  mutable std::mutex mutex_;
  std::condition_variable work_, ended_;
  std::deque<std::function<void()>> jobs_;
  std::size_t threads_ = 0, free_ = 0;
  bool stopping_ = false;
};

}
