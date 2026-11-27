#pragma once

#include <functional>
#include <string>
#include <vector>

#include "platform/host/Process.h"
#include "platform/host/app/win32/WatchedProcess.h"

namespace awtrix::hostapp::win32 {

class TiedHost : public WatchedProcess {
 public:
  explicit TiedHost(std::function<void()> signal);
  ~TiedHost() override;
  TiedHost(const TiedHost&) = delete;
  TiedHost& operator=(const TiedHost&) = delete;

  bool start(const std::vector<std::string>& args) override;
  void stop() override;
  bool ended(int& exitCode) override;
  long id() const { return process_.id(); }

 private:
  void unwatch();

  os::TiedProcess process_;
  std::function<void()> signal_;
  void* wait_ = nullptr;
};

}
