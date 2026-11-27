#pragma once

#include <dispatch/dispatch.h>

#include <functional>
#include <string>
#include <vector>

#include "platform/host/Process.h"
#include "platform/host/app/HostSupervisor.h"

namespace awtrix::hostapp {

class DispatchHostProcess : public HostProcess {
 public:
  using Ended = std::function<void(int exitCode)>;

  DispatchHostProcess(dispatch_queue_t queue, Ended ended);
  ~DispatchHostProcess() override;
  DispatchHostProcess(const DispatchHostProcess&) = delete;
  DispatchHostProcess& operator=(const DispatchHostProcess&) = delete;

  bool start(const std::vector<std::string>& args) override;
  void stop() override;
  long id() const { return process_.id(); }

 private:
  static constexpr int kReapMs = 1000;

  void exited();
  void unwatch();

  dispatch_queue_t queue_;
  Ended ended_;
  os::TiedProcess process_;
  dispatch_source_t exit_ = nullptr;
};

}
