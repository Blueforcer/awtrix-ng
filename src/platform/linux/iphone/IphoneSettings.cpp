#include "core/render/TextEncoding.h"
#include "platform/linux/iphone/IphoneSettings.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "core/api/JsonReader.h"
#include "core/icons/IconSource.h"
#include "platform/linux/iphone/IphonePayload.h"

namespace awtrix::iphone {
namespace {

using api::JsonReader;
using api::JsonWriter;

struct Failure {
  std::string message;
  std::string field;
};

bool given(const JsonReader& r) { return api::present(r) && !r.isNull(); }

bool readText(const JsonReader& r, std::size_t most, std::string& out) {
  out.clear();
  if (!r.isString() || !r.appendString(out)) return false;
  const std::size_t n = text::codepoints(out);
  return n >= 1 && n <= most;
}

bool readRule(const JsonReader& entry, const std::string& at, AppRule& rule, Failure& failure) {
  if (!entry.isObject()) {
    failure = {"expected an object", at};
    return false;
  }
  JsonReader id, name, icon;
  api::readMembers(entry.valueText(), {{"id", &id}, {"name", &name}, {"icon", &icon}});
  if (!readText(id, Settings::kMaxIdChars, rule.id)) {
    failure = {"1 to 128 characters", at + ".id"};
    return false;
  }
  if (!readText(name, Settings::kMaxNameChars, rule.name)) {
    failure = {"1 to 64 characters", at + ".name"};
    return false;
  }
  if (given(icon)) {
    if (!icon.isString() || !icon.appendString(rule.icon) || !icons::valid(rule.icon)) {
      failure = {icons::kInvalidMessage, at + ".icon"};
      return false;
    }
    if (rule.icon.size() > Settings::kMaxIconBytes) {
      failure = {"at most 6000 bytes", at + ".icon"};
      return false;
    }
  }
  return true;
}

bool readRules(JsonReader r, std::vector<AppRule>& out, Failure& failure) {
  out.clear();
  if (!r.isArray() || !r.enterArray()) {
    failure = {"expected an array", "apps"};
    return false;
  }
  std::set<std::string> ids;
  while (r.nextElement()) {
    if (out.size() == Settings::kMaxApps) {
      failure = {"at most 32 apps", "apps"};
      return false;
    }
    const std::string at = "apps[" + std::to_string(out.size()) + "]";
    AppRule rule;
    if (!readRule(r, at, rule, failure)) return false;
    if (!ids.insert(rule.id).second) {
      failure = {"duplicate id", at + ".id"};
      return false;
    }
    out.push_back(std::move(rule));
    if (!r.skipValue()) break;
  }
  return r.ok();
}

bool readFlag(const JsonReader& r, const char* name, bool& out, Failure& failure) {
  if (!api::present(r)) return true;
  if (!r.asBool(out)) {
    failure = {"expected true or false", name};
    return false;
  }
  return true;
}

void writeApps(JsonWriter& w, const std::vector<AppRule>& apps) {
  w.beginArray();
  for (const AppRule& app : apps) {
    w.beginObject().key("id").value(app.id).key("name").value(app.name);
    if (!app.icon.empty()) w.key("icon").value(app.icon);
    w.endObject();
  }
  w.endArray();
}

}

const AppRule* Settings::rule(const std::string& id) const {
  for (const AppRule& app : apps)
    if (app.id == id) return &app;
  return nullptr;
}

void Settings::saw(const std::string& id, const std::string& title, long long at) {
  seen.erase(std::remove_if(seen.begin(), seen.end(), [&](const Seen& s) { return s.id == id; }), seen.end());
  seen.insert(seen.begin(), Seen{id, clip(title, kMaxSeenTitleChars), at});
  if (seen.size() > kMaxSeen) seen.resize(kMaxSeen);
}

bool applyUpdate(const std::string& json, Settings& settings, bool& malformed, std::string& message,
                 std::string& field) {
  malformed = !api::isWellFormed(json) || !JsonReader(json).isObject();
  if (malformed) {
    message = "invalid JSON";
    field.clear();
    return false;
  }
  JsonReader enabled, music, covers, apps;
  api::readMembers(json, {{"enabled", &enabled}, {"music", &music}, {"covers", &covers}, {"apps", &apps}});
  Settings next = settings;
  Failure failure;
  const bool ok = readFlag(enabled, "enabled", next.enabled, failure) && readFlag(music, "music", next.music, failure) &&
                  readFlag(covers, "covers", next.covers, failure) &&
                  (!api::present(apps) || readRules(apps, next.apps, failure));
  if (!ok) {
    message = failure.message;
    field = failure.field;
    return false;
  }
  settings = std::move(next);
  return true;
}

void writeSettings(JsonWriter& w, const Settings& settings) {
  w.beginObject().key("music").value(settings.music).key("covers").value(settings.covers).key("apps");
  writeApps(w, settings.apps);
  w.endObject();
}

void writePhone(JsonWriter& w, const Settings& settings) {
  if (!settings.havePhone) {
    w.null();
    return;
  }
  w.beginObject().key("addr").value(settings.phone.str()).key("name").value(settings.phoneName).endObject();
}

void writeSeen(JsonWriter& w, const Settings& settings) {
  w.beginArray();
  for (const Seen& s : settings.seen)
    w.beginObject().key("id").value(s.id).key("title").value(s.title).key("at").value(s.at).endObject();
  w.endArray();
}

std::string toFile(const Settings& settings) {
  std::string out;
  JsonWriter w(out);
  w.beginObject().key("enabled").value(settings.enabled).key("music").value(settings.music).key("covers")
      .value(settings.covers).key("apps");
  writeApps(w, settings.apps);
  w.key("phone");
  if (settings.havePhone)
    w.beginObject().key("addr").value(settings.phone.str()).key("random").value(settings.phone.random).key("name")
        .value(settings.phoneName).endObject();
  else
    w.null();
  w.key("seen");
  writeSeen(w, settings);
  w.endObject();
  return out;
}

Settings fromFile(const std::string& text) {
  Settings s;
  if (!api::isWellFormed(text) || !JsonReader(text).isObject()) return s;
  JsonReader enabled, music, covers, apps, phone, seen;
  api::readMembers(text, {{"enabled", &enabled}, {"music", &music}, {"covers", &covers}, {"apps", &apps},
                          {"phone", &phone}, {"seen", &seen}});
  enabled.asBool(s.enabled);
  music.asBool(s.music);
  covers.asBool(s.covers);
  Failure ignored;
  if (api::present(apps) && !readRules(apps, s.apps, ignored)) s.apps.clear();
  if (phone.isObject()) {
    std::string addr;
    bool random = false;
    api::memberValue(phone, "addr").appendString(addr);
    api::memberValue(phone, "random").asBool(random);
    s.havePhone = ble::Address::parse(addr, random, s.phone);
    if (s.havePhone) api::memberValue(phone, "name").appendString(s.phoneName);
  }
  if (seen.enterArray()) {
    while (seen.nextElement() && s.seen.size() < Settings::kMaxSeen) {
      Seen entry;
      long long at = 0;
      if (api::memberValue(seen, "id").appendString(entry.id) && !entry.id.empty()) {
        api::memberValue(seen, "title").appendString(entry.title);
        if (api::memberValue(seen, "at").asLong(at)) entry.at = at;
        s.seen.push_back(std::move(entry));
      }
      if (!seen.skipValue()) break;
    }
  }
  return s;
}

}
