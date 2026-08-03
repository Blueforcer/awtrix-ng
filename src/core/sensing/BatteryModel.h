#pragma once

#include <cstdint>

namespace awtrix {

inline constexpr float kDefaultBatteryDividerRatio = 1.79f;

float cellVoltsFromPinMillivolts(int pinMillivolts, float dividerRatio);

uint8_t socFromVolts(float cellVolts);

}
