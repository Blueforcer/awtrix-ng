#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <string>

#include "platform/posix/WorkerThread.h"

namespace awtrix::remote {

// The clock's HTTP API as the script host reaches it: every request carries the pairing key as a
// Bearer token and no Origin, runs on a worker thread, and its answer is handed back on the main
// loop through poll(). A clock that does not answer only costs the worker its timeout.
class ClockApi {
 public:
  using Done = std::function<void(int status, const std::string& body)>;

  ClockApi(std::string origin, std::string keyHex);
  ~ClockApi();
  ClockApi(const ClockApi&) = delete;
  ClockApi& operator=(const ClockApi&) = delete;

  // Where requests queued from now on go: "http://address:port".
  void setOrigin(std::string origin) { origin_ = std::move(origin); }
  const std::string& origin() const { return origin_; }
  // Queues a request; done, when given, runs on the main loop with status 0 for no answer. Beyond
  // kMaxWaiting the oldest waiting request is dropped and answered so.
  void request(const char* method, const std::string& path, std::string body = {}, Done done = {});
  // Runs the answers that arrived; call once per frame.
  void poll();

 private:
  static constexpr std::size_t kMaxWaiting = 32;

  struct Call {
    const char* method = "";
    std::string url;
    std::string body;
    Done done;
  };

  void serve();
  void answer(Done done, int status, std::string body);

  std::string origin_;
  const std::string authorization_;
  std::mutex mutex_;
  std::deque<Call> waiting_;
  std::deque<std::function<void()>> answers_;
  posix::WorkerThread worker_;
};

}
