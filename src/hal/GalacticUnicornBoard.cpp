#include "hal/GalacticUnicornBoard.h"

namespace awtrix {
DeviceConfig galacticUnicornDefaults() {
  DeviceConfig cfg;
  cfg.panelWidth = 53;
  cfg.panelHeight = 11;
  cfg.panels = 1;
  cfg.scriptingEnabled = false;
  return cfg;
}

GalacticUnicornBoard::GalacticUnicornBoard(const DeviceConfig& cfg)
    : height_(cfg.panelHeight >= 8 && cfg.panelHeight <= 11 ? cfg.panelHeight : 11) {}
}
