#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "platform/linux/net/HttpClient.h"

namespace awtrix::net {

struct TransferDownload {
  std::string path;
  std::size_t maxBytes = 0;
  std::function<bool(const std::string& contentType)> acceptType;
  std::function<bool(const std::string& path, std::string& contentType)> verify;
  std::function<bool(std::size_t more)> room;
};

struct TransferRequest {
  std::string owner;
  int id = 0;
  std::string method, url;
  RequestBody body;
  RequestHeaders headers;
  int64_t timeoutMs = 0;
  bool followRedirects = false;
  AnswerFilter filter;
  std::optional<TransferDownload> download;
};

struct TransferResult {
  std::string owner;
  int id = 0;
  int status = 0;
  bool ok = false, noRoom = false;
  std::string body;
  std::string file, contentType;
  std::size_t fileBytes = 0;
};

class TransferQueue {
 public:
  explicit TransferQueue(std::size_t perOwner, FillRequest fill = {});
  ~TransferQueue();
  TransferQueue(const TransferQueue&) = delete;
  TransferQueue& operator=(const TransferQueue&) = delete;

  bool start(TransferRequest request);
  std::vector<int> cancel(const std::string& owner);
  bool poll(TransferResult& result);
  void stop();

 private:
  void run();
  TransferResult perform(TransferRequest& request);
  void download(httplib::Client& client, const std::string& target, TransferRequest& request, TransferResult& result);
  bool runs(const std::string& owner) const;

  const std::size_t perOwner_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<TransferRequest> queue_;
  std::deque<TransferResult> results_;
  std::string runningOwner_;
  int runningId_ = 0;
  bool running_ = false, cancelled_ = false, stopping_ = false;
  RequestRunner requests_;
  std::thread worker_;
};

}
