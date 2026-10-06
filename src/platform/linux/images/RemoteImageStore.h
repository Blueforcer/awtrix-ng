#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "media/RemoteImages.h"

namespace awtrix::images {

// Fetches one picture and fits it to width x height. Called on the store's worker thread.
class IPictureLoader {
 public:
  virtual ~IPictureLoader() = default;
  virtual bool load(const std::string& url, int width, int height, media::RemoteImage& out) = 0;
  // Any thread: ends a running load at once, and every later one.
  virtual void abort() = 0;
};

struct StoreLimits {
  // Settled pictures and their URLs; the least recently asked for go first.
  std::size_t budgetBytes = 128 * 1024;
  std::size_t maxEntries = 64;
  // Pictures waiting for the worker. Beyond that, a new one is pending until the next settles.
  std::size_t maxWaiting = 8;
  // How long a failed picture rests before it is fetched again, after its first, second and
  // every later failure.
  int64_t backoffMs[3] = {30000, 120000, 600000};
};

// The pictures from URLs of the whole display, fetched one at a time off the render thread and
// kept fitted to the size they were asked for. The same URL at another size is another picture.
// When a failed picture's backoff runs out, the generation moves on, so whoever still shows it
// asks again and so has it fetched again.
class RemoteImageStore final : public media::IRemoteImages {
 public:
  RemoteImageStore(IPictureLoader& loader, std::function<int64_t()> clock, StoreLimits limits = {});
  ~RemoteImageStore() override;
  RemoteImageStore(const RemoteImageStore&) = delete;
  RemoteImageStore& operator=(const RemoteImageStore&) = delete;

  void start();
  // For good: ends a running fetch, and every picture not yet settled stays pending.
  void stop();

  uint32_t generation() const override { return generation_.load(std::memory_order_acquire); }
  media::RemoteState get(std::string_view url, int width, int height, media::RemoteImage& out) override;

 private:
  struct Entry {
    std::string url;
    int width = 0;
    int height = 0;
    media::RemoteState state = media::RemoteState::kPending;
    media::RemoteImage image;
    uint8_t failures = 0;
    int64_t retryAtMs = 0;
    // Whether the end of the backoff has moved the generation on.
    bool announced = true;
    std::size_t bytes() const { return url.size() + image.bytes(); }
  };
  using Slot = std::list<Entry>::iterator;

  void run();
  void queue(Slot entry);
  // Moves the generation on for every backoff that has run out; the next one to run out, or
  // INT64_MAX.
  int64_t announceRetries(int64_t nowMs);
  // Drops settled pictures, least recently asked for first, until there is room for extra
  // bytes and one more entry. Waiting ones stay.
  void makeRoom(std::size_t extraBytes, std::size_t extraEntries);

  IPictureLoader& loader_;
  std::function<int64_t()> clock_;
  StoreLimits limits_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::thread worker_;
  bool stopping_ = false;
  std::list<Entry> entries_;
  std::deque<Slot> waiting_;
  std::size_t bytes_ = 0;
  std::atomic<uint32_t> generation_{0};
};

}
