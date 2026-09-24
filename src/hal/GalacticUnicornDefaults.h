#pragma once
#include "persistence/DeviceConfig.h"
namespace awtrix {
inline DeviceConfig galacticUnicornDefaults() {
  DeviceConfig cfg;
  cfg.panelWidth = 53;
  cfg.panelHeight = 11;
  cfg.panels = 1;
  cfg.scriptingEnabled = false;
  return cfg;
}
}
