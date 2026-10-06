#include "core/apps/BuiltinAppConfig.h"

#include "core/Settings.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

namespace awtrix::builtinconfig {
namespace {

enum App : unsigned { Time = 1, Date = 2, Temperature = 4, Humidity = 8, Battery = 16 };

// The settings each built-in offers. Kind, range and choices come from the settings table;
// weekdayBar.<member> is a member of the app's own weekday bar.
struct Field {
  unsigned apps;
  const char* key;
  const char* group;
  ClockProfile profile = ClockProfile::Any;
};

constexpr Field kFields[] = {
    {Time, "timeMode", "time", kClassicClockProfile},
#include "platform_settings/ClockFields.inc"
    {Time, "time24h", "time"},
    {Time, "timeLeadingZero", "time"},
    {Time, "timeShowSeconds", "time"},
    {Time, "timeShowAmPm", "time", kClassicClockProfile},
    {Time, "timeSeparatorMode", "time"},
    {Time, "timeColor", "time"},
    {Time, "calendarHeaderColor", "calendar"},
    {Time, "calendarTextColor", "calendar"},
    {Time, "calendarBodyColor", "calendar"},
#include "platform_settings/CalendarFields.inc"
    {Date, "dateOrder", "date"},
    {Date, "dateSeparator", "date"},
    {Date, "dateYearMode", "date"},
    {Date, "dateShowWeekday", "date"},
    {Date, "dateMonthNames", "date"},
    {Date, "dateColor", "date"},
    {Temperature, "useCelsius", nullptr},
    {Temperature, "temperatureColor", nullptr},
    {Humidity, "humidityColor", nullptr},
    {Battery, "batteryColor", nullptr},
    {Time | Date, "weekdayBar.show", "weekday"},
    {Time | Date, "weekdayBar.startOnMonday", "weekday"},
    {Time | Date, "weekdayBar.weekendDays", "weekday"},
    {Time | Date, "weekdayBar.activeColor", "weekday"},
    {Time | Date, "weekdayBar.inactiveColor", "weekday"},
    {Time | Date, "weekdayBar.weekendActiveColor", "weekday"},
    {Time | Date, "weekdayBar.weekendInactiveColor", "weekday"},
};

constexpr std::string_view kBar = "weekdayBar.";

unsigned appOf(std::string_view name) {
  if (name == "Time") return Time;
  if (name == "Date") return Date;
  if (name == "Temperature") return Temperature;
  if (name == "Humidity") return Humidity;
  if (name == "Battery") return Battery;
  return 0;
}

std::string_view rootOf(std::string_view key) { return key.substr(0, key.find('.')); }

bool offered(const Field& f, unsigned app, bool faces) {
  return (f.apps & app) && BuiltinAppProfile::offers(f.profile, faces);
}

bool owns(unsigned app, std::string_view root, bool faces) {
  for (const Field& f : kFields)
    if (offered(f, app, faces) && rootOf(f.key) == root) return true;
  return false;
}

// An app's int setting is a numbered choice, such as the layouts of timeMode.
void writeShape(api::JsonWriter& w, std::string_view key) {
  SettingInfo s;
  if (!Settings::describe(key, s)) return;
  switch (s.kind) {
    case SettingKind::Bool: w.member("type", "bool"); break;
    case SettingKind::Int:
      w.member("type", "select").key("options").beginArray();
      for (int i = s.lo; i <= s.hi; ++i) w.value(i);
      w.endArray();
      break;
    case SettingKind::Enum:
      w.member("type", "select").key("options").beginArray();
      for (int i = 0; i < s.count; ++i) w.value(s.names[i]);
      w.endArray();
      break;
    case SettingKind::Color: w.member("type", "color"); break;
    case SettingKind::ColorNull: w.member("type", "color").member("nullable", true); break;
    default: break;
  }
}

void writeBarShape(api::JsonWriter& w, std::string_view member) {
  if (member == "weekendDays") w.member("type", "days");
  else if (member == "show" || member == "startOnMonday") w.member("type", "bool");
  else w.member("type", "color");
}

void writeBarValue(api::JsonWriter& w, const WeekdayBarConfig& bar, std::string_view member) {
  if (member == "show") w.value(bar.show);
  else if (member == "startOnMonday") w.value(bar.startOnMonday);
  else if (member == "activeColor") w.value(bar.activeColor);
  else if (member == "inactiveColor") w.value(bar.inactiveColor);
  else if (member == "weekendActiveColor") w.value(bar.weekendActiveColor);
  else if (member == "weekendInactiveColor") w.value(bar.weekendInactiveColor);
  else {
    w.beginArray();
    for (unsigned i = 0; i < 7; ++i)
      if (bar.weekendMask & (1u << i)) w.value(weekdaybar::kDayNames[i]);
    w.endArray();
  }
}

void writeValue(api::JsonWriter& w, const Settings& settings, std::string_view key) {
  const SettingValue v = settings.read(key);
  switch (v.type) {
    case SettingValue::Type::Bool: w.value(v.b); break;
    case SettingValue::Type::Int: w.value(v.i); break;
    case SettingValue::Type::Real: w.value(v.f); break;
    case SettingValue::Type::Text: w.value(v.s); break;
    case SettingValue::Type::None: w.null(); break;
  }
}

}

bool hasApp(std::string_view name) { return appOf(name) != 0; }

void appendConfigJson(std::string& out, std::string_view name, const Settings& settings,
                      bool clockFaces) {
  const unsigned app = appOf(name);
  const Settings defaults;
  const WeekdayBarConfig& factoryBar = app == Date ? defaults.dateWeekdayBar : defaults.weekdayBar;
  const WeekdayBarConfig& bar = app == Date ? settings.dateWeekdayBar : settings.weekdayBar;
  api::JsonWriter w(out);
  w.beginObject().member("name", name).key("fields").beginArray();
  for (const Field& f : kFields) {
    if (!offered(f, app, clockFaces)) continue;
    const std::string_view key(f.key);
    const bool inBar = key.substr(0, kBar.size()) == kBar;
    const std::string_view member = inBar ? key.substr(kBar.size()) : key;
    w.beginObject().member("key", key);
    if (inBar) writeBarShape(w, member);
    else writeShape(w, key);
    if (f.group) w.member("group", f.group);
    w.key("path").beginArray();
    if (inBar) w.value(rootOf(key));
    w.value(member).endArray();
    w.key("default");
    if (inBar) writeBarValue(w, factoryBar, member);
    else writeValue(w, defaults, key);
    w.key("value");
    if (inBar) writeBarValue(w, bar, member);
    else writeValue(w, settings, key);
    w.endObject();
  }
  w.endArray().key("warnings").beginArray().endArray().endObject();
}

DispatchResult applyPatch(std::string_view name, std::string_view patch, bool clockFaces,
                          Settings& settings, bool& changed, DispatchDetail& detail) {
  changed = false;
  if (!api::isWellFormed(patch)) return DispatchResult::ParseError;
  api::JsonReader r(patch);
  if (!r.isObject() || !r.enterObject()) {
    detail.message = "expected an object";
    return DispatchResult::ValidationError;
  }
  const unsigned app = appOf(name);
  if (!app) {
    detail.message = "no settings";
    return DispatchResult::ValidationError;
  }
  std::string mapped;
  api::JsonWriter w(mapped);
  w.beginObject();
  while (r.nextMember()) {
    if (!owns(app, r.key(), clockFaces)) {
      detail = {std::string(r.key()), "unknown setting"};
      return DispatchResult::ValidationError;
    }
    const std::string key(app == Date && r.key() == "weekdayBar" ?
                              std::string_view("dateWeekdayBar") : r.key());
    w.key(key.c_str()).raw(r.valueText());
    if (!r.skipValue()) return DispatchResult::ParseError;
  }
  w.endObject();
  SettingsError err;
  if (!Settings::validateRead(api::JsonReader(mapped), err)) {
    if (app == Date && err.field.compare(0, sizeof("dateWeekdayBar") - 1, "dateWeekdayBar") == 0)
      err.field.replace(0, sizeof("dateWeekdayBar") - 1, "weekdayBar");
    detail = {err.field, err.message};
    return DispatchResult::ValidationError;
  }
  changed = settings.applyRead(api::JsonReader(mapped)) > 0;
  return DispatchResult::Ok;
}

}
