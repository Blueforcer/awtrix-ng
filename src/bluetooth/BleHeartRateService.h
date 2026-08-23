#pragma once

#include <cstdint>

#include "bluetooth/HeartRateState.h"

namespace awtrix {
namespace bluetooth {

// Autonomous BLE Heart Rate Profile client. The deliberately small public surface is also the
// seam later phases can consume without exposing NimBLE types to the rest of the firmware.
class BleHeartRateService : public IHeartRateState {
 public:
  void begin();
  void tick(uint32_t nowMs);

  bool connected() const override;
  bool hasValidBpm() const override;
  uint16_t bpm() const override;

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}
}
