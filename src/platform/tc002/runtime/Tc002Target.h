#pragma once

#include "platform/linux/LinuxTarget.h"
#include "platform/tc002/runtime/Tc002ClockApp.h"
#include "core/apps/builtin/TempApp.h"
#include "core/apps/builtin/HumidityApp.h"
#include "platform/tc002/runtime/Tc002StatusApp.h"

namespace awtrix {

class Tc002Target {
 public:
  Tc002Target();
  Tc002Target(const Tc002Target&) = delete;
  Tc002Target& operator=(const Tc002Target&) = delete;
  const LinuxTargetPolicy& policy() const { return policy_; }

 private:
  Tc002ClockApp clock_;
  Tc002StatusApp status_;
  TempApp temperature_;
  HumidityApp humidity_;
  LinuxTargetPolicy policy_;
};

}
