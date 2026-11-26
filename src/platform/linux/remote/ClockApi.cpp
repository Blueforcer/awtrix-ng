#include "platform/linux/remote/ClockApi.h"

#include <utility>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/net/HttpClient.h"

namespace awtrix::remote {

namespace {
constexpr std::size_t kMaxAnswer = 64 * 1024;
}

ClockApi::ClockApi(std::string origin, std::string keyHex)
    : origin_(std::move(origin)), authorization_("Bearer " + keyHex) {}

ClockApi::~ClockApi() = default;

void ClockApi::answer(Done done, int status, std::string body) {
  if (!done) return;
  std::lock_guard<std::mutex> lock(mutex_);
  answers_.push_back([done = std::move(done), status, body = std::move(body)] { done(status, body); });
}

void ClockApi::request(const char* method, const std::string& path, std::string body, Done done) {
  Done dropped;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (waiting_.size() >= kMaxWaiting) {
      dropped = std::move(waiting_.front().done);
      waiting_.pop_front();
    }
    waiting_.push_back({method, origin_ + path, std::move(body), std::move(done)});
  }
  answer(std::move(dropped), 0, {});
  if (worker_.post([this] { serve(); })) return;
  Done refused;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (waiting_.empty()) return;
    refused = std::move(waiting_.back().done);
    waiting_.pop_back();
  }
  answer(std::move(refused), 0, {});
}

void ClockApi::serve() {
  Call call;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (waiting_.empty()) return;
    call = std::move(waiting_.front());
    waiting_.pop_front();
  }
  std::string origin, target, body;
  int status = 0;
  if (net::splitUrl(call.url, origin, target)) {
    httplib::Client client(origin);
    net::configure(client, {5000, 2, false, 3});
    net::RequestHeaders headers{{"Authorization", authorization_}};
    if (!call.body.empty()) headers.emplace_back("Content-Type", "application/json");
    net::ResponseState state;
    const auto result = net::exchange(client, call.method, target, std::move(call.body), headers, {kMaxAnswer, {}, {}},
                                      state, [&body](const char* bytes, std::size_t count) {
                                        body.append(bytes, count);
                                        return true;
                                      });
    if (result) status = state.status;
    else body.clear();
  }
  answer(std::move(call.done), status, std::move(body));
}

void ClockApi::poll() {
  std::deque<std::function<void()>> ready;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ready.swap(answers_);
  }
  for (auto& answer : ready) answer();
}

}
