#include "platform/linux/images/RemoteImageStore.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <utility>

namespace awtrix::images {

RemoteImageStore::RemoteImageStore(IPictureLoader& loader, std::function<int64_t()> clock, StoreLimits limits)
    : loader_(loader), clock_(std::move(clock)), limits_(limits) {}

RemoteImageStore::~RemoteImageStore() { stop(); }

void RemoteImageStore::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (worker_.joinable() || stopping_) return;
  worker_ = std::thread([this] { run(); });
}

void RemoteImageStore::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  loader_.abort();
  if (worker_.joinable()) worker_.join();
}

media::RemoteState RemoteImageStore::get(std::string_view url, int width, int height,
                                         media::RemoteImage& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) {
    return e.width == width && e.height == height && e.url == url;
  });
  if (found == entries_.end()) {
    if (stopping_ || !worker_.joinable()) return media::RemoteState::kFailed;
    if (waiting_.size() >= limits_.maxWaiting) return media::RemoteState::kPending;
    makeRoom(url.size(), 1);
    if (entries_.size() >= limits_.maxEntries) return media::RemoteState::kPending;
    entries_.emplace_front();
    Entry& entry = entries_.front();
    entry.url.assign(url.data(), url.size());
    entry.width = width;
    entry.height = height;
    bytes_ += entry.bytes();
    queue(entries_.begin());
    return media::RemoteState::kPending;
  }
  entries_.splice(entries_.begin(), entries_, found);
  Entry& entry = entries_.front();
  switch (entry.state) {
    case media::RemoteState::kPending:
      return media::RemoteState::kPending;
    case media::RemoteState::kReady:
      return out.copyFrom(entry.image) ? media::RemoteState::kReady : media::RemoteState::kFailed;
    case media::RemoteState::kFailed:
      if (stopping_ || clock_() < entry.retryAtMs || waiting_.size() >= limits_.maxWaiting)
        return media::RemoteState::kFailed;
      entry.state = media::RemoteState::kPending;
      queue(entries_.begin());
      return media::RemoteState::kPending;
  }
  return media::RemoteState::kFailed;
}

void RemoteImageStore::queue(Slot entry) {
  waiting_.push_back(entry);
  wake_.notify_one();
}

void RemoteImageStore::makeRoom(std::size_t extraBytes, std::size_t extraEntries) {
  for (auto it = entries_.end(); it != entries_.begin() &&
                                 (bytes_ + extraBytes > limits_.budgetBytes ||
                                  entries_.size() + extraEntries > limits_.maxEntries);) {
    --it;
    if (it->state == media::RemoteState::kPending) continue;
    bytes_ -= it->bytes();
    it = entries_.erase(it);
  }
}

int64_t RemoteImageStore::announceRetries(int64_t nowMs) {
  int64_t next = INT64_MAX;
  bool due = false;
  for (Entry& e : entries_) {
    if (e.state != media::RemoteState::kFailed || e.announced) continue;
    if (e.retryAtMs <= nowMs) {
      e.announced = true;
      due = true;
    } else {
      next = std::min(next, e.retryAtMs);
    }
  }
  if (due) generation_.fetch_add(1, std::memory_order_release);
  return next;
}

void RemoteImageStore::run() {
  for (;;) {
    Slot entry;
    std::string url;
    int width = 0, height = 0;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      for (;;) {
        if (stopping_) return;
        const int64_t now = clock_();
        const int64_t next = announceRetries(now);
        if (!waiting_.empty()) break;
        if (next == INT64_MAX) wake_.wait(lock);
        else wake_.wait_for(lock, std::chrono::milliseconds(next - now));
      }
      entry = waiting_.front();
      waiting_.pop_front();
      url = entry->url;
      width = entry->width;
      height = entry->height;
    }
    media::RemoteImage image;
    const bool loaded = loader_.load(url, width, height, image);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) return;
      // Moved to the front before room is made.
      entries_.splice(entries_.begin(), entries_, entry);
      if (loaded && url.size() + image.bytes() <= limits_.budgetBytes) {
        entry->image = std::move(image);
        entry->state = media::RemoteState::kReady;
        entry->failures = 0;
        bytes_ += entry->image.bytes();
      } else {
        entry->state = media::RemoteState::kFailed;
        entry->retryAtMs = clock_() + limits_.backoffMs[std::min<std::size_t>(entry->failures, 2)];
        entry->announced = false;
        if (entry->failures < UINT8_MAX) ++entry->failures;
      }
      makeRoom(0, 0);
    }
    generation_.fetch_add(1, std::memory_order_release);
  }
}

}
