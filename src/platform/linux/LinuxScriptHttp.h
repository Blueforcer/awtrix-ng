#pragma once
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
  explicit LinuxScriptHttp(net::FillRequest fill = {}) : requests_(std::move(fill)) {}
  ~LinuxScriptHttp();
  void begin(std::function<void(script::HttpResult)> result);
  void stop();
  bool request(const script::HttpRequest& request) override;
 private:
  struct Job {
    std::string owner;
    script::HttpRequest request;
  };
  void run();
  std::function<void(script::HttpResult)> result_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Job> queue_;
  std::thread worker_;
  net::RequestRunner requests_;
  bool stopping_ = false;
};
}
