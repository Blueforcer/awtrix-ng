#include "../support.h"
#include "platform/linux/LinuxButtonWebhook.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/linux/host/vendor/httplib.h"

using namespace awtrix;

namespace {

using awtrix::test::require;

int availablePort() {
  const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return 0;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t size = sizeof(address);
  const bool okay = ::bind(fd, reinterpret_cast<sockaddr*>(&address), size) == 0 &&
      ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0;
  ::close(fd);
  return okay ? ntohs(address.sin_port) : 0;
}

// A listener that records every call and can hold the first one open until released.
class Listener {
 public:
  Listener() : port_(availablePort()) {
    require(port_ > 0, "free loopback port");
    server_.Post("/hook", [this](const httplib::Request& request, httplib::Response& response) {
      std::unique_lock<std::mutex> lock(mutex_);
      calls_.push_back(request.get_param_value("room") + " " + request.get_header_value("Content-Type") +
                       " " + request.body);
      changed_.notify_all();
      changed_.wait(lock, [this] { return !holding_; });
      response.status = 204;
    });
    thread_ = std::thread([this] { server_.listen("127.0.0.1", port_); });
    server_.wait_until_ready();
  }
  ~Listener() {
    release();
    server_.stop();
    thread_.join();
  }
  std::string url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/hook?room=hall"; }
  void hold() {
    std::lock_guard<std::mutex> lock(mutex_);
    holding_ = true;
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex_);
    holding_ = false;
    changed_.notify_all();
  }
  bool waitFor(std::size_t count) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, std::chrono::seconds(5), [&] { return calls_.size() >= count; });
  }
  std::vector<std::string> calls() {
    std::lock_guard<std::mutex> lock(mutex_);
    return calls_;
  }

 private:
  int port_;
  httplib::Server server_;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::vector<std::string> calls_;
  bool holding_ = false;
};

std::string call(const std::string& body) { return "hall application/json " + body; }

void pressesArriveInOrder() {
  Listener listener;
  LinuxButtonWebhook webhook("uid1");
  webhook.begin();
  webhook.press(listener.url(), "left", true);
  webhook.press(listener.url(), "left", false);
  webhook.press(listener.url(), "middle", true);
  require(listener.waitFor(3), "three presses arrive");
  const auto calls = listener.calls();
  require(calls.size() == 3, "no extra calls");
  require(calls[0] == call("{\"button\":\"left\",\"state\":true,\"uid\":\"uid1\"}"), "left down body");
  require(calls[1] == call("{\"button\":\"left\",\"state\":false,\"uid\":\"uid1\"}"), "left up body");
  require(calls[2] == call("{\"button\":\"middle\",\"state\":true,\"uid\":\"uid1\"}"), "middle down body");
}

void waitingTurnsMerge() {
  Listener listener;
  LinuxButtonWebhook webhook("uid2");
  webhook.begin();
  listener.hold();
  webhook.press(listener.url(), "knob", true);
  require(listener.waitFor(1), "the first call is in flight");
  for (int i = 0; i < 3; ++i) webhook.turn(listener.url(), 1);
  webhook.turn(listener.url(), -1);
  webhook.press(listener.url(), "knob", false);
  webhook.turn(listener.url(), -1);
  webhook.turn(listener.url(), 1);
  webhook.turn(listener.url(), -1);
  listener.release();
  require(listener.waitFor(4), "the merged calls arrive");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const auto calls = listener.calls();
  require(calls.size() == 4, "waiting turns merge into one call each run");
  require(calls[1] == call("{\"button\":\"knob\",\"turn\":2,\"uid\":\"uid2\"}"), "net turn before the release");
  require(calls[2] == call("{\"button\":\"knob\",\"state\":false,\"uid\":\"uid2\"}"), "release keeps its place");
  require(calls[3] == call("{\"button\":\"knob\",\"turn\":-1,\"uid\":\"uid2\"}"), "counterclockwise is negative");
}

void cancellingTurnsSendNothing() {
  Listener listener;
  LinuxButtonWebhook webhook("uid3");
  webhook.begin();
  listener.hold();
  webhook.press(listener.url(), "right", true);
  require(listener.waitFor(1), "the first call is in flight");
  webhook.turn(listener.url(), 1);
  webhook.turn(listener.url(), -1);
  listener.release();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  require(listener.calls().size() == 1, "a turn back and forth sends nothing");
}

void fullQueueDropsNewCalls() {
  Listener listener;
  LinuxButtonWebhook webhook("uid4");
  webhook.begin();
  listener.hold();
  webhook.press(listener.url(), "left", true);
  require(listener.waitFor(1), "the first call is in flight");
  for (std::size_t i = 0; i < LinuxButtonWebhook::kQueueLimit + 5; ++i)
    webhook.press(listener.url(), "right", i % 2 == 0);
  listener.release();
  require(listener.waitFor(1 + LinuxButtonWebhook::kQueueLimit), "the queued calls arrive");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  require(listener.calls().size() == 1 + LinuxButtonWebhook::kQueueLimit, "calls beyond the limit are dropped");
}

void onlyPlainHttpIsCalled() {
  Listener listener;
  LinuxButtonWebhook webhook("uid5");
  webhook.begin();
  webhook.press("https://127.0.0.1/hook", "left", true);
  webhook.press("http://", "left", true);
  webhook.press("", "left", true);
  webhook.turn("ftp://127.0.0.1/hook", 1);
  webhook.press(listener.url(), "left", true);
  require(listener.waitFor(1), "the plain http call arrives");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  require(listener.calls().size() == 1, "other addresses send nothing");
}

void stopCancelsAHangingListener() {
  Listener listener;
  LinuxButtonWebhook webhook("uid6");
  webhook.begin();
  listener.hold();
  webhook.press(listener.url(), "left", true);
  require(listener.waitFor(1), "the call hangs in the listener");
  const auto started = std::chrono::steady_clock::now();
  webhook.stop();
  require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(1000),
          "stop does not wait for the listener");
  webhook.press(listener.url(), "left", false);
  listener.release();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  require(listener.calls().size() == 1, "a stopped webhook sends nothing");
}

}

int main() {
  pressesArriveInOrder();
  waitingTurnsMerge();
  cancellingTurnsSendNothing();
  fullQueueDropsNewCalls();
  onlyPlainHttpIsCalled();
  stopCancelsAHangingListener();
  std::puts("Button webhook contracts passed");
}
