#include "core/apps/SensorFormat.h"

#include <cmath>
#include <cstdio>

namespace awtrix {

namespace {
std::string roundToString(float v, int decimals) {
  char buf[16];
  if (decimals <= 0) {
    snprintf(buf, sizeof(buf), "%ld", std::lround(v));
  } else {
    snprintf(buf, sizeof(buf), "%.*f", decimals, v);
  }
  return std::string(buf);
}
}

std::string formatTemperature(float celsius, bool useCelsius, int decimals) {
  const float v = useCelsius ? celsius : (celsius * 9.0f / 5.0f + 32.0f);
  std::string out = roundToString(v, decimals);
  // UTF-8 degree sign.
  out += "\xC2\xB0";
  out += useCelsius ? 'C' : 'F';
  return out;
}

std::string formatHumidity(float humidity) {
  std::string out = roundToString(humidity, 0);
  out += '%';
  return out;
}

std::string formatBattery(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  return std::to_string(percent) + "%";
}

}
