#pragma once

#include "platform/host/app/HostSupervisor.h"

namespace awtrix::hostapp::win32 {

class WatchedProcess : public HostProcess {
 public:
  virtual bool ended(int& exitCode) = 0;
};

}
