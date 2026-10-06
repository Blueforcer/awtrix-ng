#pragma once

#include <cstdint>
#include <string>

namespace awtrix {

std::string formatTemperature(float celsius, bool useCelsius, int decimals = 0);
std::string formatHumidity(float humidity);
std::string formatBattery(int percent);
inline constexpr uint32_t batteryLevelColor(int percent, bool low) {
  return low || percent < 20 ? 0xFF2000u : percent < 40 ? 0xFFA000u : 0x00E000u;
}

}
