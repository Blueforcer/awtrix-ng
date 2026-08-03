#include "core/WeekdayBarConfig.h"

#include <cstring>

#include "core/JsonColor.h"
#include "core/render/Color.h"

namespace awtrix {
namespace weekdaybar {

namespace {

// The position in this array is the bit position in weekendMask, so it has to start at Sunday.
const char* const kDayNames[] = {"sunday",   "monday", "tuesday", "wednesday",
                                 "thursday", "friday", "saturday"};

bool dayIndex(const char* name, int& index) {
  if (!name) return false;
  for (int i = 0; i < 7; ++i) {
    if (std::strcmp(kDayNames[i], name) == 0) {
      index = i;
      return true;
    }
  }
  return false;
}

bool readBool(api::JsonReader r, bool& dst, const char* field, Error& err) {
  bool v = false;
  if (!r.asBool(v)) {
    err = {field, "must be a boolean"};
    return false;
  }
  dst = v;
  return true;
}

bool readColorField(api::JsonReader r, uint32_t& dst, const char* field, Error& err) {
  uint32_t c;
  if (!color::readColor(r, c)) {
    err = {field, "must be a color"};
    return false;
  }
  dst = c;
  return true;
}

bool readWeekendDays(api::JsonReader r, uint8_t& dst, Error& err) {
  const Error bad{"weekdayBar.weekendDays", "must be an array of weekday names"};
  if (!r.isArray() || !r.enterArray()) {
    err = bad;
    return false;
  }
  uint8_t mask = 0;
  while (r.nextElement()) {
    std::string name;
    int index;
    if (!r.isString() || !r.appendString(name) || !dayIndex(name.c_str(), index)) {
      err = bad;
      return false;
    }
    mask |= static_cast<uint8_t>(1u << index);
    if (!r.skipValue()) {
      err = bad;
      return false;
    }
  }
  if (!r.ok()) {
    err = bad;
    return false;
  }
  dst = mask;
  return true;
}

}

bool read(api::JsonReader r, WeekdayBarConfig& out, Error& err) {
  err = Error{};
  if (r.isNull()) return true;
  if (!r.isObject() || !r.enterObject()) {
    err = {"weekdayBar", "must be an object"};
    return false;
  }

  while (r.nextMember()) {
    const std::string key(r.key());
    bool ok;
    if (key == "show")
      ok = readBool(r, out.show, "weekdayBar.show", err);
    else if (key == "startOnMonday")
      ok = readBool(r, out.startOnMonday, "weekdayBar.startOnMonday", err);
    else if (key == "weekendDays")
      ok = readWeekendDays(r, out.weekendMask, err);
    else if (key == "activeColor")
      ok = readColorField(r, out.activeColor, "weekdayBar.activeColor", err);
    else if (key == "inactiveColor")
      ok = readColorField(r, out.inactiveColor, "weekdayBar.inactiveColor", err);
    else if (key == "weekendActiveColor")
      ok = readColorField(r, out.weekendActiveColor, "weekdayBar.weekendActiveColor", err);
    else if (key == "weekendInactiveColor")
      ok = readColorField(r, out.weekendInactiveColor, "weekdayBar.weekendInactiveColor", err);
    else {
      err = {"weekdayBar." + key, "unknown field"};
      ok = false;
    }
    if (!ok) return false;
    if (!r.skipValue()) return false;
  }
  return r.ok();
}

void write(api::JsonWriter& w, const WeekdayBarConfig& cfg) {
  w.beginObject();
  w.member("show", cfg.show);
  w.member("startOnMonday", cfg.startOnMonday);
  w.key("weekendDays");
  w.beginArray();
  for (int i = 0; i < 7; ++i)
    if ((cfg.weekendMask >> i) & 1u) w.value(kDayNames[i]);
  w.endArray();
  w.member("activeColor", color::toHex(cfg.activeColor));
  w.member("inactiveColor", color::toHex(cfg.inactiveColor));
  w.member("weekendActiveColor", color::toHex(cfg.weekendActiveColor));
  w.member("weekendInactiveColor", color::toHex(cfg.weekendInactiveColor));
  w.endObject();
}

}
}
