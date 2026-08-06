#include "core/Settings.h"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "core/JsonColor.h"
#include "core/StrCase.h"
#include "core/api/JsonCoerce.h"
#include "core/Transitions.h"
#include "core/render/Color.h"

namespace awtrix {

namespace {

enum class Kind : uint8_t {
  Bool,
  Int,
  LongMs,
  Float,
  Enum,
  Color,
  ColorNull,
  Transition,
};

// One table row per setting: the JSON key, how to read it, and a pointer-to-member into Settings.
// Writing, reading, applying and validating all walk this same table, so a new setting is one line.
struct Field {
  const char* key;
  Kind kind;
  int lo = 0, hi = 0;
  const char* const* names = nullptr;
  int nNames = 0;
  union {
    bool Settings::*b;
    int Settings::*i;
    long Settings::*l;
    float Settings::*f;
    uint32_t Settings::*c;
    OptColor Settings::*oc;
  };
  constexpr Field(const char* k, Kind kd, bool Settings::*m) : key(k), kind(kd), b(m) {}
  constexpr Field(const char* k, Kind kd, int Settings::*m, int lo_ = 0, int hi_ = 0,
                  const char* const* nm = nullptr, int n = 0)
      : key(k), kind(kd), lo(lo_), hi(hi_), names(nm), nNames(n), i(m) {}
  constexpr Field(const char* k, Kind kd, long Settings::*m) : key(k), kind(kd), l(m) {}
  constexpr Field(const char* k, Kind kd, float Settings::*m) : key(k), kind(kd), f(m) {}
  constexpr Field(const char* k, Kind kd, uint32_t Settings::*m) : key(k), kind(kd), c(m) {}
  constexpr Field(const char* k, Kind kd, OptColor Settings::*m) : key(k), kind(kd), oc(m) {}
};

constexpr Field mkBool(const char* k, bool Settings::*m) { return {k, Kind::Bool, m}; }
constexpr Field mkInt(const char* k, int Settings::*m, int lo, int hi) {
  return {k, Kind::Int, m, lo, hi};
}
constexpr Field mkLong(const char* k, long Settings::*m) { return {k, Kind::LongMs, m}; }
constexpr Field mkFloat(const char* k, float Settings::*m) { return {k, Kind::Float, m}; }
constexpr Field mkEnum(const char* k, int Settings::*m, const char* const* names, int n) {
  return {k, Kind::Enum, m, 0, 0, names, n};
}
constexpr Field mkColor(const char* k, uint32_t Settings::*m) { return {k, Kind::Color, m}; }
constexpr Field mkNullColor(const char* k, OptColor Settings::*m) {
  return {k, Kind::ColorNull, m};
}
constexpr Field mkTransition(const char* k, int Settings::*m) { return {k, Kind::Transition, m}; }

const char* const kSepModeNames[] = {"steady", "blink", "pulse"};
const char* const kDateOrderNames[] = {"dayMonthYear", "monthDayYear", "yearMonthDay"};
const char* const kDateSepNames[] = {"dot", "slash", "dash"};
const char* const kYearModeNames[] = {"none", "twoDigit", "fourDigit"};

constexpr Field kFields[] = {
    mkBool("autoBrightness", &Settings::autoBrightness),
    mkInt("brightness", &Settings::brightness, 0, 255),
    mkBool("autoTransition", &Settings::autoTransition),
    mkColor("textColor", &Settings::textColor),
    mkTransition("transitionEffect", &Settings::transitionEffect),
    mkInt("transitionDurationMs", &Settings::transitionDurationMs, 0, INT_MAX),
    mkLong("appDurationMs", &Settings::appDurationMs),
    mkInt("timeMode", &Settings::timeMode, 0, 6),
    mkColor("calendarHeaderColor", &Settings::calendarHeaderColor),
    mkColor("calendarTextColor", &Settings::calendarTextColor),
    mkColor("calendarBodyColor", &Settings::calendarBodyColor),
    mkBool("time24h", &Settings::time24h),
    mkBool("timeLeadingZero", &Settings::timeLeadingZero),
    mkBool("timeShowSeconds", &Settings::timeShowSeconds),
    mkBool("timeShowAmPm", &Settings::timeShowAmPm),
    mkEnum("timeSeparatorMode", &Settings::timeSeparatorMode, kSepModeNames, 3),
    mkEnum("dateOrder", &Settings::dateOrder, kDateOrderNames, 3),
    mkEnum("dateSeparator", &Settings::dateSeparator, kDateSepNames, 3),
    mkEnum("dateYearMode", &Settings::dateYearMode, kYearModeNames, 3),
    mkBool("dateShowWeekday", &Settings::dateShowWeekday),
    mkBool("dateMonthNames", &Settings::dateMonthNames),
    mkBool("useCelsius", &Settings::useCelsius),
    mkBool("blockNavigation", &Settings::blockNavigation),
    mkBool("soundEnabled", &Settings::soundEnabled),
    mkBool("uppercase", &Settings::uppercase),
    mkNullColor("timeColor", &Settings::timeColor),
    mkNullColor("dateColor", &Settings::dateColor),
    mkNullColor("humidityColor", &Settings::humidityColor),
    mkNullColor("temperatureColor", &Settings::temperatureColor),
    mkNullColor("batteryColor", &Settings::batteryColor),
    mkInt("volume", &Settings::volume, 0, 30),
    mkInt("radioVolume", &Settings::radioVolume, 0, 100),
    mkBool("radioMeta", &Settings::radioMeta),
    mkInt("saturation", &Settings::saturation, 0, 100),
    mkFloat("gamma", &Settings::gamma),
    mkNullColor("colorCorrection", &Settings::colorCorrection),
    mkNullColor("colorTint", &Settings::colorTint),
};

const Field* fields(size_t& count) {
  count = sizeof(kFields) / sizeof(kFields[0]);
  return kFields;
}

int transitionIndex(const char* name) {
  if (!name) return -1;
  for (std::size_t i = 0; i < kTransitionCount; ++i)
    if (strcase::equalsIgnoreCase(kTransitionNames[i], name)) return static_cast<int>(i);
  return -1;
}

int enumIndex(const Field& f, const char* name) {
  if (!name) return -1;
  for (int i = 0; i < f.nNames; ++i)
    if (strcase::equalsIgnoreCase(f.names[i], name)) return i;
  return -1;
}

std::string enumChoices(const Field& f) {
  std::string msg = "must be one of:";
  for (int i = 0; i < f.nNames; ++i) {
    msg += ' ';
    msg += f.names[i];
  }
  return msg;
}

const Field* findField(std::string_view key) {
  size_t n;
  const Field* fs = fields(n);
  for (size_t i = 0; i < n; ++i)
    if (key == fs[i].key) return &fs[i];
  return nullptr;
}

bool enumName(api::JsonReader r, std::string& out) {
  return r.isString() && r.appendString(out);
}

}

void Settings::writeMembers(api::JsonWriter& w) const {
  size_t n;
  const Field* fs = fields(n);
  for (size_t idx = 0; idx < n; ++idx) {
    const Field& f = fs[idx];
    switch (f.kind) {
      case Kind::Bool: w.member(f.key, this->*f.b); break;
      case Kind::Int: w.member(f.key, this->*f.i); break;
      case Kind::LongMs: w.member(f.key, this->*f.l); break;
      case Kind::Float: w.member(f.key, this->*f.f); break;
      case Kind::Enum: {
        const int i = this->*f.i;
        w.member(f.key, (i >= 0 && i < f.nNames) ? f.names[i] : f.names[0]);
        break;
      }
      case Kind::Color: w.member(f.key, color::toHex(this->*f.c)); break;
      case Kind::ColorNull: {
        const OptColor& v = this->*f.oc;
        if (!v.set) w.memberNull(f.key);
        else w.member(f.key, color::toHex(v.rgb));
        break;
      }
      case Kind::Transition: {
        const int i = this->*f.i;
        w.member(f.key, (i >= 0 && i < static_cast<int>(kTransitionCount)) ? kTransitionNames[i]
                                                                           : kTransitionNames[0]);
        break;
      }
    }
  }
  w.key("scroll");
  scroll::write(w, scrollDefaults);
  w.key("weekdayBar");
  weekdaybar::write(w, weekdayBar);
}

const char* Settings::canonicalKey(std::string_view key) {
  const Field* f = findField(key);
  return f ? f->key : nullptr;
}

SettingValue Settings::read(std::string_view key) const {
  const Field* f = findField(key);
  if (!f) return SettingValue::none();
  switch (f->kind) {
    case Kind::Bool: return SettingValue::ofBool(this->*f->b);
    case Kind::Int: return SettingValue::ofInt(this->*f->i);
    case Kind::LongMs: return SettingValue::ofInt(this->*f->l);
    case Kind::Float: return SettingValue::ofReal(this->*f->f);
    case Kind::Enum: {
      const int i = this->*f->i;
      return SettingValue::ofText((i >= 0 && i < f->nNames) ? f->names[i] : f->names[0]);
    }
    case Kind::Color: return SettingValue::ofInt(static_cast<long>(this->*f->c));
    case Kind::ColorNull: {
      const OptColor& v = this->*f->oc;
      return v.set ? SettingValue::ofInt(static_cast<long>(v.rgb)) : SettingValue::none();
    }
    case Kind::Transition: {
      const int i = this->*f->i;
      return SettingValue::ofText(
          (i >= 0 && i < static_cast<int>(kTransitionCount)) ? kTransitionNames[i]
                                                             : kTransitionNames[0]);
    }
  }
  return SettingValue::none();
}

// Applies whatever it recognises and quietly clamps numbers into range. Run validateRead first if
// the caller has to reject a bad request instead of absorbing it.
int Settings::applyRead(api::JsonReader r) {
  if (!r.isObject() || !r.enterObject()) return 0;
  int applied = 0;
  while (r.nextMember()) {
    const std::string_view key = r.key();
    if (const Field* f = findField(key)) {
      switch (f->kind) {
        case Kind::Bool: this->*f->b = api::coerceBool(r); ++applied; break;
        case Kind::Int: {
          long val = api::coerceInt<long>(r);
          if (val < f->lo) val = f->lo;
          else if (val > f->hi) val = f->hi;
          this->*f->i = static_cast<int>(val);
          ++applied;
          break;
        }
        case Kind::LongMs: {
          const long val = api::coerceInt<long>(r);
          this->*f->l = val < 0 ? 0 : val;
          ++applied;
          break;
        }
        case Kind::Float: {
          const float fv = api::coerceFloat(r);
          this->*f->f = fv > 0.0f ? fv : this->*f->f;
          ++applied;
          break;
        }
        case Kind::Enum: {
          std::string name;
          const int i = enumName(r, name) ? enumIndex(*f, name.c_str()) : -1;
          if (i >= 0) { this->*f->i = i; ++applied; }
          break;
        }
        case Kind::Color: {
          uint32_t c;
          if (color::readColor(r, c)) { this->*f->c = c; ++applied; }
          break;
        }
        case Kind::ColorNull: {
          if (r.isNull()) { this->*f->oc = OptColor{}; ++applied; break; }
          uint32_t c;
          if (color::readColor(r, c)) { this->*f->oc = OptColor{c, true}; ++applied; }
          break;
        }
        case Kind::Transition: {
          std::string name;
          const int i = enumName(r, name) ? transitionIndex(name.c_str()) : -1;
          if (i >= 0) { this->*f->i = i; ++applied; }
          break;
        }
      }
    } else if (key == "scroll") {
      ScrollSpec s;
      scroll::Error err;
      if (scroll::read(r, s, err)) {
        if (s.hasMode) scrollDefaults.mode = s.mode;
        if (s.hasDirection) scrollDefaults.direction = s.direction;
        if (s.hasEntry) scrollDefaults.entry = s.entry;
        if (s.hasWhenFits) scrollDefaults.whenFits = s.whenFits;
        if (s.hasSpeed) scrollDefaults.speed = s.speed;
        if (s.hasGap) scrollDefaults.gap = s.gap;
        if (s.hasHoldMs) scrollDefaults.holdMs = s.holdMs;
        ++applied;
      }
    } else if (key == "weekdayBar") {
      weekdaybar::Error err;
      if (weekdaybar::read(r, weekdayBar, err)) ++applied;
    }
    if (!r.skipValue()) break;
  }
  return applied;
}

// Checks the whole object before a single field is applied, so a bad key cannot leave settings
// half changed. Unknown keys fail here, where applyRead would just skip them.
bool Settings::validateRead(api::JsonReader r, SettingsError& err) {
  if (!r.isObject() || !r.enterObject()) return true;
  while (r.nextMember()) {
    const std::string key(r.key());
    const Field* f = findField(key);
    if (!f) {
      bool ok;
      if (key == "scroll") {
        ScrollSpec ignored;
        scroll::Error se;
        ok = scroll::read(r, ignored, se);
        if (!ok) err = {se.field, se.message};
      } else if (key == "weekdayBar") {
        WeekdayBarConfig ignored;
        weekdaybar::Error we;
        ok = weekdaybar::read(r, ignored, we);
        if (!ok) err = {we.field, we.message};
      } else {
        err = {key, "unknown field"};
        ok = false;
      }
      if (!ok) return false;
      if (!r.skipValue()) return false;
      continue;
    }
    switch (f->kind) {
      case Kind::Bool:
        if (!r.isBool()) { err = {key, "must be a boolean"}; return false; }
        break;
      case Kind::Int: {
        long long v = 0;
        if (!r.isNumber() || !r.isInteger() || !r.asLong(v) || v < LONG_MIN || v > LONG_MAX) {
          err = {key, "must be an integer"};
          return false;
        }
        if (v < f->lo || v > f->hi) {
          err = {key, "out of range"};
          return false;
        }
        break;
      }
      case Kind::LongMs: {
        long long v = 0;
        if (!r.isNumber() || !r.isInteger() || !r.asLong(v) || v < 0 || v > LONG_MAX) {
          err = {key, "must be a non-negative integer (milliseconds)"};
          return false;
        }
        break;
      }
      case Kind::Float: {
        double d = 0.0;
        if (!r.isNumber() || !r.asDouble(d) || static_cast<float>(d) <= 0.0f) {
          err = {key, "must be a positive number"};
          return false;
        }
        break;
      }
      case Kind::Enum: {
        std::string name;
        if (!enumName(r, name) || enumIndex(*f, name.c_str()) < 0) {
          err = {key, enumChoices(*f)};
          return false;
        }
        break;
      }
      case Kind::Color: {
        uint32_t c;
        if (!color::readColor(r, c)) {
          err = {key, "must be a color (\"#RGB\", \"#RRGGBB\", [r,g,b], [\"HSV\",h,s,v] or a packed integer)"};
          return false;
        }
        break;
      }
      case Kind::ColorNull: {
        if (r.isNull()) break;
        uint32_t c;
        if (!color::readColor(r, c)) {
          err = {key, "must be a color or null"};
          return false;
        }
        break;
      }
      case Kind::Transition: {
        std::string name;
        if (!enumName(r, name) || transitionIndex(name.c_str()) < 0) {
          std::string choices = "must be one of: ";
          for (std::size_t i = 0; i < kTransitionCount; ++i) {
            if (i) choices += ", ";
            choices += kTransitionNames[i];
          }
          err = {key, choices};
          return false;
        }
        break;
      }
    }
    if (!r.skipValue()) return false;
  }
  return r.ok();
}

}
