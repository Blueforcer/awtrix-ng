#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include "core/script/ScriptServices.h"
#include "platform/linux/net/HttpClient.h"

namespace awtrix {
class LinuxScriptHttp : public script::IScriptHttp {
 public:
  ~LinuxScriptHttp();
  void begin(std::function<void(script::HttpResult)> result);
  void stop();
  bool request(const script::HttpRequest& request) override;
 private:
  void run();
  std::function<void(script::HttpResult)> result_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<script::HttpRequest> queue_;
  std::thread worker_;
  net::SocketInterrupt interrupt_;
  bool stopping_ = false;
};
}
