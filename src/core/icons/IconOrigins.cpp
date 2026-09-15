#include "core/icons/IconOrigins.h"

#include <algorithm>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

namespace awtrix::iconorigins {
namespace {
bool identifier(const std::string& value, bool upper) {
  if (value.empty() || value.size() > 32) return false;
  for (const char c : value)
    if (!((c >= 'a' && c <= 'z') || (upper && c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  return true;
}

bool validHub(const std::string& hub) {
  if (hub.size() > 240 || hub.size() < 16 || hub.rfind("https://", 0) != 0 ||
      hub.compare(hub.size() - 7, 7, "/icons/") != 0) return false;
  // Do not admit userinfo, fragments, query strings, escaped authority delimiters,
  // backslashes or dot-segments. No request is ever made by the firmware itself.
  for (const unsigned char c : hub)
    if (c <= 32 || c >= 127 || c == '@' || c == '?' || c == '#' || c == '%' ||
        c == '\\') return false;
  const auto slash = hub.find('/', 8);
  if (slash == std::string::npos || slash == 8) return false;
  const std::string authority = hub.substr(8, slash - 8);
  const auto colon = authority.find(':');
  const std::string host = authority.substr(0, colon);
  if (host.empty() || host.front() == '.' || host.back() == '.' ||
      host.front() == '-' || host.back() == '-' || host.find("..") != std::string::npos)
    return false;
  for (const char c : host)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
  if (colon != std::string::npos) {
    const std::string port = authority.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    unsigned value = 0;
    for (const char c : port) {
      if (c < '0' || c > '9') return false;
      value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value == 0 || value > 65535) return false;
  }
  const std::string path = hub.substr(slash);
  if (path.find("//") != std::string::npos || path.find("/../") != std::string::npos ||
      path.find("/./") != std::string::npos) return false;
  for (const char c : path)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '/' || c == '.' || c == '-' || c == '_'))
      return false;
  return true;
}

bool parseRecord(api::JsonReader r, Record& out) {
  if (!r.isObject() || !r.enterObject()) return false;
  unsigned seen = 0;
  while (r.nextMember()) {
    unsigned bit = 0;
    std::string* dest = nullptr;
    if (r.keyEquals("name")) { bit = 1; dest = &out.name; }
    else if (r.keyEquals("hub")) { bit = 2; dest = &out.hub; }
    else if (r.keyEquals("slug")) { bit = 4; dest = &out.slug; }
    else if (r.keyEquals("sha256")) { bit = 8; dest = &out.sha256; }
    if (dest && ((seen & bit) || !r.isString() || !r.appendString(*dest))) return false;
    seen |= bit;
    if (!r.skipValue()) return false;
  }
  if (seen != 15 || !validName(out.name) || !validHub(out.hub) ||
      !identifier(out.slug, false) || out.sha256.size() != 64) return false;
  for (const char c : out.sha256)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return r.ok();
}

Result error(int status, const char* code, const char* message) {
  std::string out;
  api::JsonWriter w(out);
  w.beginObject(); w.key("error"); w.beginObject();
  w.member("code", code); w.member("message", message);
  w.endObject(); w.endObject();
  return {status, std::move(out)};
}

bool load(Backend& storage, std::vector<Record>& records) {
  std::string json;
  if (!storage.read(json)) return false;
  if (!json.empty() && !parseCollection(json, records)) return false;
  return true;
}
}

bool validName(const std::string& name) {
  if (name.size() <= 4 || name.size() > 36) return false;
  const std::string ext = name.substr(name.size() - 4);
  return (ext == ".gif" || ext == ".jpg") && identifier(name.substr(0, name.size() - 4), true);
}

bool parseCollection(const std::string& json, std::vector<Record>& out) {
  out.clear();
  if (json.size() > kMaxBytes || !api::isWellFormed(json)) return false;
  api::JsonReader root(json);
  if (!root.isObject() || !root.enterObject()) return false;
  bool seen = false;
  while (root.nextMember()) {
    if (root.keyEquals("icons")) {
      if (seen || !root.isArray()) return false;
      seen = true;
      auto items = root;
      if (!items.enterArray()) return false;
      while (items.nextElement()) {
        Record record;
        if (out.size() >= kMaxRecords || !parseRecord(items, record)) return false;
        for (const auto& prev : out) if (prev.name == record.name) return false;
        out.push_back(std::move(record));
        if (!items.skipValue()) return false;
      }
    }
    if (!root.skipValue()) return false;
  }
  return seen && root.ok();
}

std::string serialize(const std::vector<Record>& records) {
  std::string out;
  api::JsonWriter w(out);
  w.beginObject(); w.key("icons"); w.beginArray();
  for (const auto& r : records) {
    w.beginObject(); w.member("name", r.name); w.member("hub", r.hub);
    w.member("slug", r.slug); w.member("sha256", r.sha256); w.endObject();
  }
  w.endArray(); w.endObject();
  return out;
}

Result handle(Backend& storage, const std::string& method, const std::string& body,
              const std::string& name) {
  if (method != "GET" && method != "PUT" && method != "DELETE")
    return error(405, "methodNotAllowed", "allowed method(s): GET, PUT, DELETE");
  Record incoming;
  if (method == "PUT") {
    if (body.size() > kMaxPutBytes) return error(413, "payloadTooLarge", "origin exceeds 1024 bytes");
    if (!api::isWellFormed(body) || !parseRecord(api::JsonReader(body), incoming))
      return error(400, "invalidOrigin", "expected valid name, HTTPS hub, slug and lowercase SHA256");
    if (!storage.iconExists(incoming.name)) return error(404, "notFound", "icon file not found");
  }
  if (method == "DELETE" && !validName(name))
    return error(400, "invalidName", "name must be an icon filename ending in .gif or .jpg");
  std::vector<Record> records;
  if (!load(storage, records)) return error(500, "storageError", "could not read icon origins");
  if (method != "DELETE") {
    records.erase(std::remove_if(records.begin(), records.end(), [&](const Record& r) {
      return !storage.iconExists(r.name);
    }), records.end());
  }
  if (method == "GET") return {200, serialize(records)};
  if (method == "DELETE") {
    const auto found = std::find_if(records.begin(), records.end(), [&](const Record& r) {
      return r.name == name;
    });
    if (found == records.end()) return {200, "{\"ok\":true}"};
    records.erase(found);
  } else {
    const auto found = std::find_if(records.begin(), records.end(), [&](const Record& r) {
      return r.name == incoming.name;
    });
    if (found != records.end()) *found = std::move(incoming);
    else {
      if (records.size() >= kMaxRecords)
        return error(507, "insufficientStorage", "at most 64 icon origins can be stored");
      records.push_back(std::move(incoming));
    }
  }
  const std::string json = serialize(records);
  if (json.size() > kMaxBytes)
    return error(507, "insufficientStorage", "icon origins exceed the 16 KiB storage limit");
  if (!storage.writeAtomic(json)) return error(500, "storageError", "could not save icon origins");
  return {200, "{\"ok\":true}"};
}

bool restore(Backend& storage, const std::string& json, std::string& err) {
  std::vector<Record> records;
  if (!parseCollection(json, records)) { err = "invalid icon origins"; return false; }
  records.erase(std::remove_if(records.begin(), records.end(), [&](const Record& r) {
    return !storage.iconExists(r.name);
  }), records.end());
  if (!storage.writeAtomic(serialize(records))) { err = "could not save icon origins"; return false; }
  return true;
}
}
