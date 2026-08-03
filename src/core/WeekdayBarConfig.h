#pragma once

#include <cstdint>
#include <string>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

namespace awtrix {

struct WeekdayBarConfig {
  bool show = true;
  bool startOnMonday = true;
  // One bit per day, bit 0 = Sunday through bit 6 = Saturday, whatever startOnMonday says about
  // how the bar is drawn. Default marks Sunday and Saturday.
  uint8_t weekendMask = (1u << 0) | (1u << 6);
  uint32_t activeColor = 0xFFFFFFu;
  uint32_t inactiveColor = 0x666666u;
  uint32_t weekendActiveColor = 0xFFFFFFu;
  uint32_t weekendInactiveColor = 0x666666u;
};

namespace weekdaybar {

struct Error {
  std::string field;
  std::string message;
};

bool read(api::JsonReader r, WeekdayBarConfig& out, Error& err);

void write(api::JsonWriter& w, const WeekdayBarConfig& cfg);

}
}
