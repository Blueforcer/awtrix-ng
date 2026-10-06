#pragma once

#include <cstdint>

namespace awtrix {

inline constexpr float kDefaultBatteryDividerRatio = 1.79f;

inline constexpr bool isLowBattery(int percent, int threshold) {
  return threshold > 0 && percent < threshold;
}

float cellVoltsFromPinMillivolts(int pinMillivolts, float dividerRatio);

uint8_t socFromVolts(float cellVolts);

}
