#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "platform/linux/iphone/IphoneCovers.h"
#include "platform/linux/net/HttpGet.h"

namespace awtrix {

// Asks the iTunes Search API for album covers on its own thread, one at a time, so a slow answer
// never stalls the panel.
class LinuxCoverFetch : public iphone::CoverSource {
 public:
  LinuxCoverFetch() = default;
  LinuxCoverFetch(const LinuxCoverFetch&) = delete;
  LinuxCoverFetch& operator=(const LinuxCoverFetch&) = delete;
  ~LinuxCoverFetch() override;
  void begin();
  void stop();
  bool lookup(const std::string& artist, const std::string& title, int size) override;
  bool result(std::string& url) override;

 private:
  struct Job {
    std::string artist, title;
    int size = 8;
  };
  void run();
  net::HttpGet http_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::thread worker_;
  bool stopping_ = false;
  bool queued_ = false;
  bool busy_ = false;
  bool finished_ = false;
  Job job_;
  std::string url_;
};

}
