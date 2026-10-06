#pragma once

#include <cstdint>
#include "core/apps/AppRegistry.h"
#include "core/render/DisplayProfile.h"
#include "core/script/BerryVM.h"

namespace awtrix {

struct LinuxTargetPolicy {
  const char* id = "linux";
  long scriptInstructionLimit = script::BerryVM::kInstructionLimit;
  DisplayLimits displayLimits = hostDisplayLimits();
  bool physicalDisplay = false;
  bool clockFaces = false;
  uint64_t uploadReserveBytes = 0;
  uint64_t maxBodyBytes = 0;
  int defaultVolume = -1;
  AppRegistry apps;
};

}
