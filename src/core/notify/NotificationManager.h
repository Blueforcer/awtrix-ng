#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "core/payload/AppSpec.h"

namespace awtrix {

class NotificationManager {
 public:
  explicit NotificationManager(std::size_t capacity) : capacity_(capacity) {}

  bool push(const AppSpec& n, int64_t nowMs);

  bool hasCurrent() const { return !queue_.empty(); }
  const AppSpec& current() const { return queue_.front(); }
  std::size_t size() const { return queue_.size(); }
  bool empty() const { return queue_.empty(); }
  // Bumped whenever the front of the queue changes, so a renderer can tell a new notification
  // from the same one still showing without comparing contents.
  uint32_t generation() const { return generation_; }

  void dismiss(int64_t nowMs);

  bool dismissNamed(const std::string& name, int64_t nowMs);

  void update(int64_t nowMs, long defaultDurationMs, bool hold = false,
              bool passesDone = false);

  void clear() { queue_.clear(); }

 private:
  std::deque<AppSpec> queue_;
  std::size_t capacity_;
  int64_t currentStartMs_ = 0;
  uint32_t generation_ = 0;
};

}
