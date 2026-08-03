#include "core/payload/ScrollSpec.h"

#include <cstring>

namespace awtrix {
namespace scroll {

namespace {

// Index-aligned with the matching enums; lookup() casts the array index straight to the enum.
const char* const kModeNames[] = {"static", "wrap", "loop", "bounce"};
const char* const kDirectionNames[] = {"left", "right"};
const char* const kEntryNames[] = {"inline", "offscreen"};
const char* const kWhenFitsNames[] = {"static", "scroll"};

template <std::size_t N>
bool lookup(const char* const (&names)[N], const char* value, uint8_t& index) {
  if (!value) return false;
  for (std::size_t i = 0; i < N; ++i) {
    if (std::strcmp(names[i], value) == 0) {
      index = static_cast<uint8_t>(i);
      return true;
    }
  }
  return false;
}

template <std::size_t N, typename E>
bool readEnum(const char* const (&names)[N], api::JsonReader r, E& dst, bool& present,
              const char* field, Error& err) {
  uint8_t index = 0;
  std::string s;
  const bool got = r.isString() && r.appendString(s) && lookup(names, s.c_str(), index);
  if (!got) {
    err = {field, "unknown value"};
    return false;
  }
  dst = static_cast<E>(index);
  present = true;
  return true;
}

template <std::size_t N, typename E>
bool parseEnum(const char* const (&names)[N], const char* value, E& out) {
  uint8_t index = 0;
  if (!lookup(names, value, index)) return false;
  out = static_cast<E>(index);
  return true;
}

bool readCount(api::JsonReader r, int& dst, bool& present, const char* field, Error& err) {
  long long v = 0;
  if (!r.isNumber() || !r.isInteger() || !r.asLong(v) || v < 0) {
    err = {field, "must be a non-negative integer"};
    return false;
  }
  dst = static_cast<int>(v);
  present = true;
  return true;
}

}

bool parseMode(const char* value, ScrollMode& out) { return parseEnum(kModeNames, value, out); }

bool parseDirection(const char* value, ScrollDirection& out) {
  return parseEnum(kDirectionNames, value, out);
}

bool parseEntry(const char* value, ScrollEntry& out) { return parseEnum(kEntryNames, value, out); }

bool parseWhenFits(const char* value, ScrollWhenFits& out) {
  return parseEnum(kWhenFitsNames, value, out);
}

void write(api::JsonWriter& w, const ScrollDefaults& d) {
  w.beginObject();
  w.member("mode", kModeNames[static_cast<uint8_t>(d.mode)]);
  w.member("direction", kDirectionNames[static_cast<uint8_t>(d.direction)]);
  w.member("entry", kEntryNames[static_cast<uint8_t>(d.entry)]);
  w.member("whenFits", kWhenFitsNames[static_cast<uint8_t>(d.whenFits)]);
  w.member("speed", d.speed);
  w.member("gap", d.gap);
  w.member("holdMs", d.holdMs);
  w.endObject();
}

// "scroll" is either a mode string as shorthand or a full object. null leaves every field unset.
bool read(api::JsonReader r, ScrollSpec& out, Error& err) {
  err = Error{};
  if (r.isNull()) return true;
  if (r.isString()) return readEnum(kModeNames, r, out.mode, out.hasMode, "scroll", err);
  if (!r.isObject()) {
    err = {"scroll", "must be an object or a mode string"};
    return false;
  }

  if (!r.enterObject()) {
    err = {"scroll", "must be an object or a mode string"};
    return false;
  }
  while (r.nextMember()) {
    const std::string key(r.key());
    bool ok;
    if (key == "mode")
      ok = readEnum(kModeNames, r, out.mode, out.hasMode, "scroll.mode", err);
    else if (key == "direction")
      ok = readEnum(kDirectionNames, r, out.direction, out.hasDirection, "scroll.direction", err);
    else if (key == "entry")
      ok = readEnum(kEntryNames, r, out.entry, out.hasEntry, "scroll.entry", err);
    else if (key == "whenFits")
      ok = readEnum(kWhenFitsNames, r, out.whenFits, out.hasWhenFits, "scroll.whenFits", err);
    else if (key == "speed")
      ok = readCount(r, out.speed, out.hasSpeed, "scroll.speed", err);
    else if (key == "gap")
      ok = readCount(r, out.gap, out.hasGap, "scroll.gap", err);
    else if (key == "holdMs")
      ok = readCount(r, out.holdMs, out.hasHoldMs, "scroll.holdMs", err);
    else {
      err = {"scroll." + key, "unknown field"};
      ok = false;
    }
    if (!ok) return false;
    if (!r.skipValue()) return false;
  }
  return r.ok();
}

}
}
