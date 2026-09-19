#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace awtrix::script {

class ScriptTimers {
 public:
  struct Entry {
    int32_t id;
    std::string owner;
    int64_t due;
    int32_t interval;
  };

  int32_t add(const std::string& owner, int32_t delay, bool repeat, int64_t now) {
    if (owner.empty() || delay < 25 || delay > 86400000 || entries_.size() >= 32 ||
        nextId_ == INT32_MAX) return 0;
    const auto count = std::count_if(entries_.begin(), entries_.end(),
                                    [&](const Entry& e) { return e.owner == owner; });
    if (count >= 8) return 0;
    const int32_t id = ++nextId_;
    entries_.push_back({id, owner, now + delay, repeat ? delay : 0});
    return id;
  }

  bool cancel(const std::string& owner, int32_t id) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const Entry& e) { return e.id == id && e.owner == owner; });
    if (it == entries_.end()) return false;
    entries_.erase(it);
    return true;
  }

  bool has(const std::string& owner) const {
    for (const auto& e : entries_)
      if (e.owner == owner) return true;
    return false;
  }

  void purge(const std::string& owner) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                 [&](const Entry& e) { return e.owner == owner; }), entries_.end());
  }

  std::vector<int32_t> due(int64_t now) const {
    std::vector<int32_t> result;
    for (const auto& e : entries_)
      if (e.due <= now) result.push_back(e.id);
    return result;
  }

  const Entry* find(int32_t id) const {
    for (const auto& e : entries_)
      if (e.id == id) return &e;
    return nullptr;
  }

  void advance(int32_t id, int64_t now) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->id != id) continue;
      if (it->interval) it->due = now + it->interval;
      else entries_.erase(it);
      return;
    }
  }

 private:
  std::vector<Entry> entries_;
  int32_t nextId_ = 0;
};

}
