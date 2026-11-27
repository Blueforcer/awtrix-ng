#pragma once

#include <Arduino.h>
#include <time.h>

namespace awtrix {
inline void writeLogLine(const char* line) { Serial.println(line); }
inline void logLocalTime(time_t now, struct tm& out) { localtime_r(&now, &out); }
}
