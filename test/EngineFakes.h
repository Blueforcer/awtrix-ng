#pragma once

#include "core/Services.h"

namespace awtrix::test {
struct NullDisplay : IDisplayService {
  void sendScreen() override {}
};
struct NullSystem : ISystemService {
  void reboot() override {}
  void sleep(uint64_t) override {}
  void factoryReset() override {}
  void resetSettings() override {}
};
}
