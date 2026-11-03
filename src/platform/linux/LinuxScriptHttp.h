#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include "core/script/ScriptServices.h"
#include "platform/linux/net/HttpClient.h"

namespace awtrix {
class LinuxScriptHttp : public script::IScriptHttp {
 public:
  explicit LinuxScriptHttp(net::FillRequest fill = {}) : quick_(fill), patient_(std::move(fill)) {}
  ~LinuxScriptHttp();
  void begin(std::function<void(script::HttpResult)> result);
  void stop();
  bool request(const script::HttpRequest& request) override;
  void cancel(uint32_t id) override;
 private:
  struct Job {
    std::string owner;
    script::HttpRequest request;
    std::chrono::steady_clock::time_point queuedAt;
  };
  struct Lane {
    explicit Lane(net::FillRequest fill) : requests(std::move(fill)) {}
    std::condition_variable ready;
    std::deque<Job> queue;
    std::thread worker;
    net::RequestRunner requests;
    uint32_t running = 0;
  };
  void run(Lane& lane);
  std::function<void(script::HttpResult)> result_;
  std::mutex mutex_;
  Lane quick_, patient_;
  std::size_t queuedBodyBytes_ = 0;
  bool stopping_ = false;
};
}
