#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace httplib { class Client; }

namespace awtrix {
// Sends the buttonCallback calls from its own thread, so a slow listener never stalls the panel.
// Knob turns still waiting merge into one call; a full queue drops new calls.
class LinuxButtonWebhook {
 public:
  static constexpr std::size_t kQueueLimit = 16;
  explicit LinuxButtonWebhook(std::string uid) : uid_(std::move(uid)) {}
  LinuxButtonWebhook(const LinuxButtonWebhook&) = delete;
  LinuxButtonWebhook& operator=(const LinuxButtonWebhook&) = delete;
  ~LinuxButtonWebhook();
  void begin();
  void stop();
  void press(const std::string& url, const char* control, bool state);
  void turn(const std::string& url, int direction);

 private:
  struct Call {
    std::string url;
    const char* control = "";
    bool state = false;
    int turn = 0;
  };
  void enqueue(Call call);
  void run();
  const std::string uid_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Call> queue_;
  std::thread worker_;
  httplib::Client* activeClient_ = nullptr;
  bool stopping_ = false;
};
}
