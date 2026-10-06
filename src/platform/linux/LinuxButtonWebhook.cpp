#include "platform/linux/LinuxButtonWebhook.h"
#include <chrono>
#include "core/input/ButtonWebhook.h"
#include "platform/linux/host/vendor/httplib.h"

namespace awtrix {
namespace {
constexpr const char* kScheme = "http://";
}

LinuxButtonWebhook::~LinuxButtonWebhook() { stop(); }

void LinuxButtonWebhook::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (worker_.joinable()) return;
  stopping_ = false;
  worker_ = std::thread([this] { run(); });
}

void LinuxButtonWebhook::stop() {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    stopping_ = true;
    queue_.clear();
    ready_.notify_all();
    while (activeClient_) {
      activeClient_->stop();
      ready_.wait_for(lock, std::chrono::milliseconds(20), [this] { return !activeClient_; });
    }
  }
  if (worker_.joinable()) worker_.join();
}

void LinuxButtonWebhook::press(const std::string& url, const char* control, bool state) {
  Call call;
  call.url = url;
  call.control = control;
  call.state = state;
  enqueue(std::move(call));
}

void LinuxButtonWebhook::turn(const std::string& url, int direction) {
  if (direction == 0) return;
  Call call;
  call.url = url;
  call.control = input::kWebhookKnobName;
  call.turn = direction;
  enqueue(std::move(call));
}

void LinuxButtonWebhook::enqueue(Call call) {
  if (call.url.rfind(kScheme, 0) != 0 || call.url.size() == std::char_traits<char>::length(kScheme)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || !worker_.joinable()) return;
  if (call.turn && !queue_.empty() && queue_.back().turn && queue_.back().url == call.url) {
    queue_.back().turn += call.turn;
    if (!queue_.back().turn) queue_.pop_back();
    return;
  }
  if (queue_.size() >= kQueueLimit) return;
  queue_.push_back(std::move(call));
  ready_.notify_one();
}

void LinuxButtonWebhook::run() {
  for (;;) {
    Call call;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) return;
      call = std::move(queue_.front());
      queue_.pop_front();
    }
    const auto slash = call.url.find_first_of("/?#", std::char_traits<char>::length(kScheme));
    std::string path = slash == std::string::npos ? "/" : call.url.substr(slash);
    if (path.front() != '/') path.insert(path.begin(), '/');
    const auto fragment = path.find('#');
    if (fragment != std::string::npos) path.resize(fragment);
    const std::string body = call.turn ? input::webhookTurnBody(call.turn, uid_)
                                       : input::webhookPressBody(call.control, call.state, uid_);
    httplib::Client client(call.url.substr(0, slash));
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(2, 0);
    client.set_write_timeout(2, 0);
    client.set_follow_location(false);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) return;
      activeClient_ = &client;
    }
    client.Post(path, body, "application/json");
    {
      std::lock_guard<std::mutex> lock(mutex_);
      activeClient_ = nullptr;
      ready_.notify_all();
      if (stopping_) return;
    }
  }
}
}
