#include "core/script/ScriptData.h"

#include <string_view>
#include <utility>
#include <vector>

#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonText.h"
#include "core/script/ScriptHeap.h"

namespace awtrix::script {
namespace {

using api::JsonReader;

bool decodeKey(std::string_view raw, std::string& out) {
  out.clear();
  if (raw.find('\\') == std::string_view::npos) {
    out.assign(raw);
    return true;
  }
  std::string quoted;
  quoted.reserve(raw.size() + 2);
  quoted += '"';
  quoted.append(raw);
  quoted += '"';
  return JsonReader(quoted).appendString(out);
}

bool enterStore(JsonReader& r, const std::string& storeJson) {
  return api::isWellFormed(storeJson) && r.isObject() && r.enterObject();
}

struct Change {
  std::string key;
  std::string_view value;
  bool remove = false;
  bool applied = false;
};

Change* find(std::vector<Change>& changes, const std::string& key) {
  for (Change& c : changes)
    if (c.key == key) return &c;
  return nullptr;
}

void appendMember(std::string& out, bool& first, const std::string& key, std::string_view value) {
  if (!first) out += ',';
  first = false;
  api::appendJsonString(out, key);
  out += ':';
  out.append(value);
}

}

bool appendDataJson(std::string& out, const ConfigSchema& schema, const std::string& storeJson) {
  const std::size_t budget = heap::growthBudget();
  out += '{';
  bool first = true;
  std::string key;
  JsonReader r(storeJson);
  if (enterStore(r, storeJson)) {
    while (r.nextMember()) {
      if (!r.isNull() && decodeKey(r.key(), key) && !declares(schema, key)) {
        appendMember(out, first, key, r.valueText());
        if (out.size() > budget) return false;
      }
      if (!r.skipValue()) break;
    }
  }
  out += '}';
  return true;
}

StorePatch applyDataPatch(const ConfigSchema& schema, const std::string& storeJson,
                          const std::string& patchJson) {
  StorePatch res;
  if (!api::isWellFormed(patchJson)) {
    res.malformed = true;
    return res;
  }
  JsonReader r(patchJson);
  if (!r.isObject()) {
    res.message = "expected an object";
    return res;
  }

  std::vector<Change> changes;
  std::string key;
  r.enterObject();
  while (r.nextMember()) {
    if (!decodeKey(r.key(), key)) {
      res.malformed = true;
      return res;
    }
    if (declares(schema, key)) {
      res.field = key;
      res.message = "'" + key + "' is a setting; use /config";
      return res;
    }
    Change* known = find(changes, key);
    if (!known) {
      changes.push_back(Change{key, {}, false, false});
      known = &changes.back();
    }
    known->remove = r.isNull();
    known->value = known->remove ? std::string_view() : r.valueText();
    if (!r.skipValue()) break;
  }
  if (!r.ok()) {
    res.malformed = true;
    return res;
  }

  std::string& out = res.storeJson;
  out += '{';
  bool first = true;
  JsonReader s(storeJson);
  if (enterStore(s, storeJson)) {
    while (s.nextMember()) {
      if (decodeKey(s.key(), key)) {
        Change* c = find(changes, key);
        if (!c) {
          appendMember(out, first, key, s.valueText());
        } else if (!c->applied) {
          c->applied = true;
          if (!c->remove) appendMember(out, first, key, c->value);
        }
      }
      if (!s.skipValue()) break;
    }
  }
  for (const Change& c : changes)
    if (!c.applied && !c.remove) appendMember(out, first, c.key, c.value);
  out += '}';
  res.ok = true;
  return res;
}

int dataResponse(const std::string& name, const ConfigTextFn& readSource,
                 const ConfigTextFn& readStore, std::string& body) {
  std::string source, storeJson;
  if (const int status =
          readScriptForResponse(name, readSource, readStore, source, storeJson, body))
    return status;
  body.clear();
  if (!appendDataJson(body, parseConfig(source), storeJson)) {
    body = api::errorJson("insufficientStorage", "not enough memory",
                          "name");
    return 507;
  }
  return 200;
}

}
