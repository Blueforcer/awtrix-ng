#include "platform/tc002/runtime/Tc002Target.h"
#include "platform/tc002/contract/tc002_layout.h"

namespace awtrix {

Tc002Target::Tc002Target() {
  policy_.id = "tc002";
  policy_.scriptInstructionLimit = 2000000;
  policy_.displayLimits = {TC002_PANEL_WIDTH, TC002_PANEL_WIDTH, TC002_PANEL_HEIGHT, TC002_PANEL_HEIGHT,
                           TC002_PANEL_WIDTH * TC002_PANEL_HEIGHT};
  policy_.physicalDisplay = true;
  policy_.clockFaces = true;
  policy_.uploadReserveBytes = 1024 * 1024;
  policy_.maxBodyBytes = 2 * 1024 * 1024;
  policy_.defaultVolume = 90;
  IApp* apps[] = {&clock_, &temperature_, &humidity_, &status_};
  for (IApp* app : apps) policy_.apps.add(app);
}

}
