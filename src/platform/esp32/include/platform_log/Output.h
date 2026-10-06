#pragma once

#include <Arduino.h>

namespace awtrix {
inline void writeLogLine(const char* line) { Serial.println(line); }
}
