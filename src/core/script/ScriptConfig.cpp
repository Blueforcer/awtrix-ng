#include "core/script/ScriptConfig.h"

#include <cmath>
#include <string_view>

#include "core/JsonColor.h"
#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonText.h"
#include "core/api/JsonWriter.h"
#include "core/render/Color.h"
#include "core/script/ScriptMeta.h"

namespace awtrix::script {
namespace {

using api::JsonReader;

constexpr std::size_t kMaxKeyLen = 24;
constexpr std::size_t kMaxLabelLen = 48;
constexpr std::size_t kMaxHelpLen = 96;
constexpr std::size_t kMaxUnitLen = 8;
constexpr std::size_t kMaxOptions = 12;
constexpr std::size_t kMaxOptionLen = 24;
constexpr std::size_t kMaxTextLen = 256;
static_assert(kMaxConfigFields == 12, "the overflow warning spells the number out");

constexpr const char* kTypeNames[] = {"bool", "text", "number", "slider", "select", "color"};

bool ieq(const std::string& a, const char* lower) {
  std::size_t i = 0;
  for (; i < a.size() && lower[i]; ++i) {
    const char c = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] + 32) : a[i];
    if (c != lower[i]) return false;
  }
  return i == a.size() && !lower[i];
}

const char* typeName(ConfigType type) {
  const std::size_t i = static_cast<std::size_t>(type);
  return i < 6 ? kTypeNames[i] : kTypeNames[1];
}

std::size_t indexOf(const ConfigSchema& schema, std::string_view key) {
  for (std::size_t i = 0; i < schema.fields.size(); ++i)
    if (key == std::string_view(schema.fields[i].key)) return i;
  return schema.fields.size();
}

bool nextOption(const std::string& list, std::size_t& at, std::string_view& out) {
  if (at > list.size()) return false;
  const std::size_t comma = list.find(',', at);
  const std::size_t end = comma == std::string::npos ? list.size() : comma;
  out = std::string_view(list).substr(at, end - at);
  at = comma == std::string::npos ? list.size() + 1 : comma + 1;
  return true;
}

bool offers(const ConfigField& f, const std::string& value) {
  std::size_t at = 0;
  std::string_view one;
  while (nextOption(f.options, at, one))
    if (one == value) return true;
  return false;
}

std::string firstOption(const std::string& list) {
  const std::size_t comma = list.find(',');
  return comma == std::string::npos ? list : list.substr(0, comma);
}

bool blank(char c) { return c == ' ' || c == '\t'; }

void skipBlank(const std::string& s, std::size_t& i) {
  while (i < s.size() && blank(s[i])) ++i;
}

// Reads one bare or "quoted" word and leaves i just past it. `quoted` is how the caller
// tells `text` the type from "text" the label, which is why it is reported and not dropped.
bool readWord(const std::string& s, std::size_t& i, std::string& out, bool& quoted) {
  skipBlank(s, i);
  out.clear();
  if (i >= s.size()) return false;
  quoted = s[i] == '"';
  if (quoted) {
    ++i;
    const std::size_t start = i;
    while (i < s.size() && s[i] != '"') ++i;
    out.assign(s, start, i - start);
    if (i < s.size()) ++i;
    return true;
  }
  const std::size_t start = i;
  while (i < s.size() && !blank(s[i])) ++i;
  out.assign(s, start, i - start);
  return !out.empty();
}

bool readAttr(const std::string& s, std::size_t& i, std::string& name, std::string& value) {
  skipBlank(s, i);
  if (i >= s.size()) return false;
  const std::size_t start = i;
  while (i < s.size() && !blank(s[i]) && s[i] != '=') ++i;
  name.assign(s, start, i - start);
  value.clear();
  if (i < s.size() && s[i] == '=') {
    ++i;
    bool quoted = false;
    readWord(s, i, value, quoted);
  }
  return !name.empty();
}

enum class Attr : uint8_t { Default, Help, Unit, Options, Min, Max, Step, MaxLen, Unknown };

Attr attrOf(const std::string& name) {
  static constexpr const char* kNames[] = {"default", "help",  "unit", "options",
                                           "min",     "max",   "step", "maxlen"};
  for (std::size_t i = 0; i < 8; ++i)
    if (ieq(name, kNames[i])) return static_cast<Attr>(i);
  return Attr::Unknown;
}

bool identChar(char c, bool first) {
  if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') return true;
  return !first && c >= '0' && c <= '9';
}

bool validKey(const std::string& k) {
  if (k.empty() || k.size() > kMaxKeyLen) return false;
  for (std::size_t i = 0; i < k.size(); ++i)
    if (!identChar(k[i], i == 0)) return false;
  return true;
}

bool parseType(const std::string& s, ConfigType& out) {
  for (std::size_t i = 0; i < 6; ++i) {
    if (!ieq(s, kTypeNames[i])) continue;
    out = static_cast<ConfigType>(i);
    return true;
  }
  return false;
}

bool toNumber(const std::string& s, double& out) {
  if (s.empty()) return false;
  double v = 0;
  if (!api::parseDouble(s.data(), s.data() + s.size(), v) || !std::isfinite(v)) return false;
  out = v;
  return true;
}

bool toBool(const std::string& s, bool& out) {
  static constexpr const char* kWords[] = {"true", "1", "yes", "on",
                                           "false", "0", "no", "off"};
  for (std::size_t i = 0; i < 8; ++i) {
    if (!ieq(s, kWords[i])) continue;
    out = i < 4;
    return true;
  }
  return false;
}

// Accepts "#RRGGBB", "0xRRGGBB" or a plain decimal, all landing on 0xRRGGBB. Anything above
// 0xFFFFFF is refused rather than masked, because it is far more likely a typo than intent.
bool toColor(const std::string& s, uint32_t& out) {
  if (s.empty()) return false;
  if (s[0] == '#') return color::tryFromHex(s, out);
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    return color::tryFromHex(s.substr(2), out);

  uint32_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    if (v > 0xFFFFFFu) return false;
    v = v * 10 + static_cast<uint32_t>(c - '0');
  }
  if (v > 0xFFFFFFu) return false;
  out = v;
  return true;
}

std::string numberJson(double v) {
  std::string out;
  api::JsonWriter w(out);
  w.value(v);
  return out;
}

std::string stringJson(const std::string& v) {
  std::string out;
  api::appendJsonString(out, v);
  return out;
}

std::string colorJson(uint32_t v) {
  std::string out;
  api::appendInt(out, static_cast<long long>(v));
  return out;
}

void clip(std::string& s, std::size_t limit) {
  if (s.size() > limit) s.resize(limit);
}

std::string splitOptions(const std::string& value) {
  std::string out;
  std::size_t at = 0;
  std::size_t count = 0;
  while (at <= value.size() && count < kMaxOptions) {
    const std::size_t comma = value.find(',', at);
    std::string one = detail::trim(
        value.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
    clip(one, kMaxOptionLen);
    if (!one.empty()) {
      if (count) out += ',';
      out += one;
      ++count;
    }
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  return out;
}

void warn(ConfigSchema& schema, int line, std::string_view a, std::string_view b = {},
          std::string_view c = {}, std::string_view d = {}) {
  std::string out = "line ";
  api::appendInt(out, line);
  out += ": ";
  out.append(a);
  out.append(b);
  out.append(c);
  out.append(d);
  schema.warnings.push_back(std::move(out));
}

bool typeMatches(const ConfigField& f, std::string_view raw) {
  JsonReader r(raw);
  switch (f.type) {
    case ConfigType::Bool:
      return r.isBool();
    case ConfigType::Text:
    case ConfigType::Select:
      return r.isString();
    case ConfigType::Number:
    case ConfigType::Slider:
      return r.isNumber();
    case ConfigType::Color: {
      long long v = 0;
      return r.isNumber() && r.asLong(v) && v >= 0 && v <= 0xFFFFFF;
    }
  }
  return false;
}

// Validates one incoming value against its field and renders the JSON to store. Wrong types
// are rejected with a reason the UI shows; out-of-range numbers are clamped instead.
bool coerce(const ConfigField& f, const JsonReader& r, std::string& out, std::string& why) {
  switch (f.type) {
    case ConfigType::Bool: {
      bool b = false;
      if (!r.asBool(b)) { why = "expected true or false"; return false; }
      out = b ? "true" : "false";
      return true;
    }
    case ConfigType::Text:
    case ConfigType::Select: {
      std::string s;
      if (!r.isString() || !r.appendString(s)) { why = "expected a string"; return false; }
      const std::size_t limit = f.maxLen ? f.maxLen : kMaxTextLen;
      if (s.size() > limit) {
        why = "longer than ";
        api::appendInt(why, static_cast<long long>(limit));
        why += " characters";
        return false;
      }
      if (f.type == ConfigType::Select && !offers(f, s)) {
        why = "not one of the offered options";
        return false;
      }
      out = stringJson(s);
      return true;
    }
    case ConfigType::Number:
    case ConfigType::Slider: {
      double v = 0;
      if (!r.asDouble(v) || !std::isfinite(v)) { why = "expected a number"; return false; }
      if (f.hasMin && v < f.min) v = f.min;
      if (f.hasMax && v > f.max) v = f.max;
      out = numberJson(v);
      return true;
    }
    case ConfigType::Color: {
      uint32_t c = 0;
      if (!color::readColor(r, c)) {
        why = "expected a colour like \"#FF8800\", a number or [r,g,b]";
        return false;
      }
      out = colorJson(c);
      return true;
    }
  }
  why = "unsupported field";
  return false;
}

// Rewrites the stored JSON with replace[i] (a JSON fragment, empty meaning "leave alone")
// substituted for field i. Keys the schema knows nothing about are copied through untouched.
std::string mergeStore(const ConfigSchema& schema, const std::string& storeJson,
                       const std::string* replace) {
  // One bit per schema field, so a value already rewritten in place is not appended again.
  uint32_t written = 0;
  std::string out;
  out += '{';
  bool first = true;
  JsonReader r(storeJson);
  if (r.isObject() && r.enterObject()) {
    while (r.nextMember()) {
      const std::size_t i = indexOf(schema, r.key());
      if (!first) out += ',';
      first = false;
      out += '"';
      out.append(r.key());
      out += "\":";
      if (i != schema.fields.size() && !replace[i].empty()) {
        out += replace[i];
        written |= 1u << i;
      } else {
        out.append(r.valueText());
      }
      if (!r.skipValue()) break;
    }
  }
  for (std::size_t i = 0; i < schema.fields.size(); ++i) {
    if ((written & (1u << i)) || replace[i].empty()) continue;
    if (!first) out += ',';
    first = false;
    api::appendJsonString(out, schema.fields[i].key);
    out += ':';
    out += replace[i];
  }
  out += '}';
  return out;
}

void applyDefault(ConfigField& f, const std::string& raw, bool given, ConfigSchema& schema,
                  int line) {
  switch (f.type) {
    case ConfigType::Bool: {
      bool b = false;
      if (given && !toBool(raw, b))
        warn(schema, line, f.key, ": default is not true or false");
      f.defJson = b ? "true" : "false";
      return;
    }
    case ConfigType::Text: {
      std::string s = raw;
      clip(s, f.maxLen ? f.maxLen : kMaxTextLen);
      f.defJson = stringJson(s);
      return;
    }
    case ConfigType::Select: {
      if (f.options.empty()) {
        f.defJson = stringJson(std::string());
        return;
      }
      std::string s = raw;
      if (!offers(f, s)) {
        if (given)
          warn(schema, line, f.key, ": default is not in options");
        s = firstOption(f.options);
      }
      f.defJson = stringJson(s);
      return;
    }
    case ConfigType::Number:
    case ConfigType::Slider: {
      double v = 0;
      if (given && !toNumber(raw, v))
        warn(schema, line, f.key, ": default is not a number");
      if (f.hasMin && v < f.min) v = f.min;
      if (f.hasMax && v > f.max) v = f.max;
      f.defJson = numberJson(v);
      return;
    }
    case ConfigType::Color: {
      uint32_t c = 0;
      if (given && !toColor(raw, c))
        warn(schema, line, f.key, ": default is not a colour");
      f.defJson = colorJson(c);
      return;
    }
  }
}

// Parses one field declaration: `key type "Label"? attr=value ...`, the label recognised by
// its quotes. An unusable line is warned about and dropped; the script itself still runs.
void parseLine(const std::string& value, int line, ConfigSchema& schema) {
  std::size_t at = 0;
  std::string word;
  bool quoted = false;

  if (!readWord(value, at, word, quoted) || quoted) {
    warn(schema, line, "@config needs a key and a type");
    return;
  }
  ConfigField f;
  f.key = word;
  if (!validKey(f.key)) {
    warn(schema, line, "'", f.key, "' is not a usable key");
    return;
  }
  if (indexOf(schema, f.key) != schema.fields.size()) {
    warn(schema, line, "'", f.key, "' is declared twice");
    return;
  }
  if (!readWord(value, at, word, quoted) || quoted || !parseType(word, f.type)) {
    warn(schema, line, f.key, ": unknown type '", word,
         "', use bool, text, number, slider, select or color");
    return;
  }

  skipBlank(value, at);
  if (at < value.size() && value[at] == '"') {
    readWord(value, at, f.label, quoted);
    clip(f.label, kMaxLabelLen);
  }
  if (f.label.empty()) f.label = f.key;

  std::string name, attr, defaultRaw;
  bool defaultGiven = false;
  while (readAttr(value, at, name, attr)) {
    switch (attrOf(name)) {
      case Attr::Default:
        defaultRaw = attr;
        defaultGiven = true;
        break;
      case Attr::Help:
        f.help = attr;
        clip(f.help, kMaxHelpLen);
        break;
      case Attr::Unit:
        f.unit = attr;
        clip(f.unit, kMaxUnitLen);
        break;
      case Attr::Options:
        f.options = splitOptions(attr);
        break;
      case Attr::Min:
        f.hasMin = toNumber(attr, f.min);
        if (!f.hasMin) warn(schema, line, f.key, ": min is not a number");
        break;
      case Attr::Max:
        f.hasMax = toNumber(attr, f.max);
        if (!f.hasMax) warn(schema, line, f.key, ": max is not a number");
        break;
      case Attr::Step:
        if (!toNumber(attr, f.step)) warn(schema, line, f.key, ": step is not a number");
        break;
      case Attr::MaxLen: {
        double v = 0;
        if (!toNumber(attr, v) || v <= 0)
          warn(schema, line, f.key, ": maxlen is not a length");
        else
          f.maxLen = static_cast<std::size_t>(v) > kMaxTextLen ? kMaxTextLen
                                                              : static_cast<std::size_t>(v);
        break;
      }
      default:
        warn(schema, line, f.key, ": unknown attribute '", name, "'");
    }
  }

  if (f.hasMin && f.hasMax && f.min > f.max) {
    warn(schema, line, f.key, ": min is above max");
    f.hasMin = f.hasMax = false;
  }
  if (f.type == ConfigType::Select && f.options.empty()) {
    warn(schema, line, f.key, ": select needs options=a,b,c");
    return;
  }
  // A slider without a range has no meaning, so give it a percentage rather than warn.
  if (f.type == ConfigType::Slider && !(f.hasMin && f.hasMax)) {
    f.hasMin = f.hasMax = true;
    f.min = 0;
    f.max = 100;
  }

  applyDefault(f, defaultRaw, defaultGiven, schema, line);
  schema.fields.push_back(std::move(f));
}

}

ConfigSchema parseConfig(const std::string& source) {
  ConfigSchema schema;
  bool overflowed = false;

  forEachHeaderTag(source, [&](const std::string& key, const std::string& value, int line) {
    if (key != "config") return;
    if (schema.fields.size() >= kMaxConfigFields) {
      if (!overflowed) {
        overflowed = true;
        warn(schema, line, "only 12 settings per script");
      }
      return;
    }
    parseLine(value, line, schema);
  });

  return schema;
}

// Fills in declared settings the store has no usable value for, so a script's first run sees
// its defaults. A value of the wrong type counts as missing: the field's type may have changed.
bool seedConfigDefaults(const ConfigSchema& schema, const std::string& storeJson,
                        std::string& out) {
  if (schema.fields.empty()) return false;

  std::string replace[kMaxConfigFields];
  for (std::size_t i = 0; i < schema.fields.size(); ++i) replace[i] = schema.fields[i].defJson;

  JsonReader r(storeJson);
  if (r.isObject() && r.enterObject()) {
    while (r.nextMember()) {
      const std::size_t i = indexOf(schema, r.key());
      if (i != schema.fields.size() && typeMatches(schema.fields[i], r.valueText()))
        replace[i].clear();
      if (!r.skipValue()) break;
    }
  }

  bool any = false;
  for (std::size_t i = 0; i < schema.fields.size(); ++i) any = any || !replace[i].empty();
  if (!any) return false;

  out = mergeStore(schema, storeJson, replace);
  return true;
}

// Removes stored values for settings the edited source no longer declares. Only keys the OLD
// source declared are dropped; anything the script wrote through store.set() survives.
bool dropUndeclaredValues(const ConfigSchema& before, const ConfigSchema& after,
                          const std::string& storeJson, std::string& out) {
  if (before.fields.empty() || storeJson.empty()) return false;

  bool gone = false;
  for (const ConfigField& f : before.fields)
    gone = gone || indexOf(after, f.key) == after.fields.size();
  if (!gone) return false;

  std::string merged;
  merged += '{';
  bool first = true;
  bool dropped = false;
  JsonReader r(storeJson);
  if (r.isObject() && r.enterObject()) {
    while (r.nextMember()) {
      const bool wasDeclared = indexOf(before, r.key()) != before.fields.size();
      if (wasDeclared && indexOf(after, r.key()) == after.fields.size()) {
        dropped = true;
        if (!r.skipValue()) break;
        continue;
      }
      if (!first) merged += ',';
      first = false;
      merged += '"';
      merged.append(r.key());
      merged += "\":";
      merged.append(r.valueText());
      if (!r.skipValue()) break;
    }
  }
  merged += '}';
  if (!dropped) return false;
  out = std::move(merged);
  return true;
}

// Builds what the settings UI renders: the schema plus each field's current value, falling
// back to the default. `warnings` is how an author learns a @config line was ignored.
void appendConfigJson(std::string& out, const std::string& name, const ConfigSchema& schema,
                      const std::string& storeJson) {
  // Views into storeJson, so it must outlive this call -- it does, it is the argument.
  std::string_view values[kMaxConfigFields];
  JsonReader r(storeJson);
  if (r.isObject() && r.enterObject()) {
    while (r.nextMember()) {
      const std::size_t i = indexOf(schema, r.key());
      const std::string_view raw = r.valueText();
      if (i != schema.fields.size() && typeMatches(schema.fields[i], raw)) values[i] = raw;
      if (!r.skipValue()) break;
    }
  }

  api::JsonWriter w(out);
  w.beginObject();
  w.member("name", name);
  w.key("fields");
  w.beginArray();
  for (std::size_t i = 0; i < schema.fields.size(); ++i) {
    const ConfigField& f = schema.fields[i];
    w.beginObject();
    w.member("key", f.key);
    w.member("type", typeName(f.type));
    w.member("label", f.label);
    if (!f.help.empty()) w.member("help", f.help);
    if (!f.unit.empty()) w.member("unit", f.unit);
    if (f.hasMin) w.member("min", f.min);
    if (f.hasMax) w.member("max", f.max);
    if (f.step != 0) w.member("step", f.step);
    if (f.maxLen) w.member("maxlen", static_cast<long long>(f.maxLen));
    if (f.type == ConfigType::Select) {
      w.key("options");
      w.beginArray();
      std::size_t at = 0;
      std::string_view one;
      while (nextOption(f.options, at, one)) w.value(one);
      w.endArray();
    }
    w.key("default");
    w.raw(f.defJson);
    w.key("value");
    w.raw(values[i].empty() ? std::string_view(f.defJson) : values[i]);
    w.endObject();
  }
  w.endArray();
  w.key("warnings");
  w.beginArray();
  for (const std::string& warning : schema.warnings) w.value(warning);
  w.endArray();
  w.endObject();
}

ConfigPatch applyConfigPatch(const ConfigSchema& schema, const std::string& storeJson,
                             const std::string& patchJson) {
  ConfigPatch res;

  if (!api::isWellFormed(patchJson)) {
    res.message = "body is not valid JSON";
    return res;
  }
  JsonReader probe(patchJson);
  if (!probe.isObject()) {
    res.message = "body must be a JSON object of setting keys";
    return res;
  }
  if (schema.fields.empty()) {
    res.message = "this script has no settings";
    return res;
  }

  std::string replace[kMaxConfigFields];
  JsonReader r(patchJson);
  r.enterObject();
  while (r.nextMember()) {
    const std::size_t i = indexOf(schema, r.key());
    if (i == schema.fields.size()) {
      res.field.assign(r.key());
      res.message = "'" + res.field + "' is not a setting of this script";
      return res;
    }
    std::string why;
    if (!coerce(schema.fields[i], r, replace[i], why)) {
      res.field = schema.fields[i].key;
      res.message = res.field + ": " + why;
      return res;
    }
    if (!r.skipValue()) break;
  }
  if (!r.ok()) {
    res.message = "body is not valid JSON";
    return res;
  }

  res.ok = true;
  res.storeJson = mergeStore(schema, storeJson, replace);
  return res;
}

int configResponse(const std::string& name, const ConfigTextFn& readSource,
                   const ConfigTextFn& readStore, std::string& body) {
  if (!readSource) {
    body = api::errorJson("unavailable", "scripting is not available");
    return 503;
  }
  if (!api::isValidAppName(name)) {
    body = api::errorJson("invalidName", "name must match [A-Za-z0-9_-]{1,32}", "name");
    return 400;
  }
  std::string source;
  if (!readSource(name, source)) {
    body = api::errorJson("notFound", "no such script");
    return 404;
  }
  std::string storeJson;
  if (readStore) readStore(name, storeJson);
  body.clear();
  appendConfigJson(body, name, parseConfig(source), storeJson);
  return 200;
}

}
