#pragma once

#include <cstdint>

namespace awtrix::bluetooth {

// Read-only heart-rate state shared with consumers that must not depend on NimBLE.
class IHeartRateState {
 public:
  virtual ~IHeartRateState() = default;

  virtual bool connected() const = 0;
  virtual bool hasValidBpm() const = 0;
  virtual uint16_t bpm() const = 0;
};

}
