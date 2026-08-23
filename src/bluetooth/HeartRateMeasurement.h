#pragma once

#include <cstddef>
#include <cstdint>

namespace awtrix {
namespace bluetooth {

struct HeartRateMeasurement {
  bool valid = false;
  uint16_t bpm = 0;
};

// Bluetooth SIG Heart Rate Measurement (0x2A37): bit zero selects the value width.
// Optional fields follow the BPM and are deliberately ignored.
inline HeartRateMeasurement parseHeartRateMeasurement(const uint8_t* data, std::size_t size) {
  if (!data || size < 2) return {};
  if ((data[0] & 0x01u) == 0) return {true, data[1]};
  if (size < 3) return {};
  return {true, static_cast<uint16_t>(static_cast<uint16_t>(data[1]) |
                                      (static_cast<uint16_t>(data[2]) << 8))};
}

}
}
