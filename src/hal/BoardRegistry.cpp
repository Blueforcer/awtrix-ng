#include "hal/BoardRegistry.h"

#include "hal/Esp32Board.h"

namespace awtrix {
// The first call builds the board from cfg; every later call returns that same instance and
// ignores its argument, so pin or layout changes only take effect after a reboot.
IBoard& activeBoard(const DeviceConfig& cfg) {
  static Esp32Board board(cfg);
  return board;
}
}
