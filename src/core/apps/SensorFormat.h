#pragma once

#include <string>

namespace awtrix {

std::string formatTemperature(float celsius, bool useCelsius, int decimals = 0);
std::string formatHumidity(float humidity);
std::string formatBattery(int percent);

}
