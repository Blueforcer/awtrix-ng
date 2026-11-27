#pragma once

#include <dispatch/dispatch.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "platform/host/app/HostSupervisor.h"
#include "platform/host/app/macos/DispatchHostProcess.h"
#include "platform/host/app/macos/WakeTimer.h"

namespace awtrix::hostapp {

class HostDriver {
 public:
  using Changed = std::function<void()>;

  HostDriver(std::vector<std::string> args, dispatch_queue_t queue, Changed changed);
  HostDriver(const HostDriver&) = delete;
  HostDriver& operator=(const HostDriver&) = delete;

  void start();
  void quit();
  HostView view() const;
  long processId() const { return process_.id(); }

 private:
  static int64_t nowMs();
  void settle();

  Changed changed_;
  DispatchHostProcess process_;
  HostSupervisor supervisor_;
  WakeTimer timer_;
};

}
