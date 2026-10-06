#include "platform/linux/iphone/LinuxCoverFetch.h"

namespace awtrix {
namespace {
constexpr std::size_t kMaxAnswerBytes = 64 * 1024;
constexpr int64_t kTimeoutMs = 8000;
}

LinuxCoverFetch::~LinuxCoverFetch() { stop(); }

void LinuxCoverFetch::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (worker_.joinable() || stopping_) return;
  worker_ = std::thread([this] { run(); });
}

void LinuxCoverFetch::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    ready_.notify_all();
  }
  http_.abort();
  if (worker_.joinable()) worker_.join();
}

bool LinuxCoverFetch::lookup(const std::string& artist, const std::string& title, int size) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || !worker_.joinable() || busy_) return false;
  job_ = Job{artist, title, size};
  busy_ = queued_ = true;
  finished_ = false;
  ready_.notify_one();
  return true;
}

bool LinuxCoverFetch::result(std::string& url) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!finished_) return false;
  finished_ = busy_ = false;
  url = std::move(url_);
  url_.clear();
  return true;
}

void LinuxCoverFetch::run() {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || queued_; });
      if (stopping_) return;
      queued_ = false;
      job = job_;
    }
    std::string answer, url;
    if (http_.fetch(iphone::coverSearchUrl(job.artist, job.title), {kMaxAnswerBytes, kTimeoutMs, false}, answer)
            .failure == net::HttpGet::Failure::None)
      url = iphone::coverImageUrl(answer, job.size);
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return;
    url_ = std::move(url);
    finished_ = true;
  }
}

}
