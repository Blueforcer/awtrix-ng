#include "platform/linux/secrets/SecretService.h"

#include <algorithm>
#include <string_view>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "core/net/Url.h"
#include "core/script/HttpHeaders.h"
#include "platform/linux/net/UrlEncode.h"
#include "platform/linux/net/UrlPolicy.h"
#include "system/Log.h"

namespace awtrix::secrets {
namespace {

using net::kPlaceholder;
constexpr std::size_t kMaxRecord = 32768;

struct Stored {
  std::string key;
  std::string hosts;
  std::string value;
};
using Values = std::vector<Stored>;

std::string hostsOf(const Secret& secret) {
  std::string joined;
  for (const std::string& host : secret.hosts) joined += (joined.empty() ? "" : ",") + host;
  return joined;
}

const Stored* find(const Values& values, std::string_view key) {
  const auto it = std::find_if(values.begin(), values.end(), [key](const Stored& s) { return s.key == key; });
  return it == values.end() ? nullptr : &*it;
}

bool parse(const std::string& json, std::string& app, Values& out) {
  if (!api::isWellFormed(json)) return false;
  const api::JsonReader record(json);
  api::JsonReader list = api::memberValue(record, "keys");
  app = api::memberText(record, "app");
  if (app.empty() || !list.enterArray()) return false;
  while (list.nextElement()) {
    out.push_back({api::memberText(list, "key"), api::memberText(list, "hosts"), api::memberText(list, "value")});
    if (!list.skipValue()) return false;
  }
  return true;
}

Values loaded(const posix::PrivateStore& store, const std::string& app) {
  std::string json, owner;
  Values values;
  if (!store.load(app, json) || !parse(json, owner, values) || owner != app) values.clear();
  return values;
}

Values bound(const Spec& spec, Values stored) {
  stored.erase(std::remove_if(stored.begin(), stored.end(),
                              [&spec](const Stored& s) {
                                const Secret* secret = spec.find(s.key);
                                return !secret || s.value.empty() || s.hosts != hostsOf(*secret);
                              }),
               stored.end());
  return stored;
}

Values current(const posix::PrivateStore& store, const std::string& app, const std::optional<Spec>& spec) {
  return spec ? bound(*spec, loaded(store, app)) : Values{};
}

bool save(posix::PrivateStore& store, const std::string& app, const Values& values) {
  if (values.empty()) return store.erase(app);
  std::string json;
  api::JsonWriter w(json);
  w.beginObject().member("app", app).key("keys").beginArray();
  for (const Stored& s : values)
    w.beginObject().member("key", s.key).member("hosts", s.hosts).member("value", s.value).endObject();
  w.endArray().endObject();
  return store.save(app, json);
}

bool placeholdersInTarget(const std::string& url) {
  if (!net::parseUrl(url)) return false;
  const std::size_t target = url.find_first_of("/?#", url.find("://") + 3);
  if (url.find(kPlaceholder) < target) return false;
  const std::size_t fragment = url.find('#', target);
  return fragment == std::string::npos || url.find(kPlaceholder, fragment) == std::string::npos;
}

const char* refusalFor(std::string_view key, const std::optional<Spec>& spec, const Values& values,
                       const std::string& url) {
  const Secret* secret = spec ? spec->find(key) : nullptr;
  if (!secret) return "unknown";
  const char* refusal = nullptr;
  if (!allows(*secret, url, &refusal)) return refusal;
  return find(values, key) ? nullptr : "empty";
}

const char* substitute(std::string& text, bool encode, const std::optional<Spec>& spec, const Values& values,
                       const std::string& url) {
  std::size_t at = text.find(kPlaceholder);
  if (at == std::string::npos) return nullptr;
  std::string out;
  out.reserve(text.size() + net::kMaxFillGrowth);
  std::size_t from = 0;
  for (; at != std::string::npos; at = text.find(kPlaceholder, from)) {
    const std::size_t begin = at + kPlaceholder.size();
    const std::size_t end = text.find('}', begin);
    if (end == std::string::npos) return "unknown";
    const std::string_view key = std::string_view(text).substr(begin, end - begin);
    if (const char* refusal = refusalFor(key, spec, values, url)) return refusal;
    const std::string& value = find(values, key)->value;
    out.append(text, from, at - from).append(encode ? net::percentEncoded(value) : value);
    from = end + 1;
    if (out.size() > from + net::kMaxFillGrowth) return "size";
  }
  out.append(text, from, std::string::npos);
  text = std::move(out);
  return nullptr;
}

}

Service::Service(std::string directory, script::SourceReader source)
    : store_(std::move(directory), kMaxRecord), source_(std::move(source)) {}

Status Service::status(const std::string& app) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Parsed& parsed = parsedLocked(app);
  Status status;
  status.invalid = parsed.invalid;
  status.declared = parsed.spec.has_value() || !parsed.invalid.empty();
  const Values values = current(store_, app, parsed.spec);
  if (parsed.spec)
    for (const Secret& secret : parsed.spec->secrets)
      status.keys.push_back({secret.key, secret.label, secret.help, secret.hosts, find(values, secret.key) != nullptr});
  return status;
}

bool Service::set(const std::string& app, const std::vector<std::pair<std::string, std::string>>& values,
                  std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Parsed& parsed = parsedLocked(app);
  if (!parsed.spec) {
    error = parsed.invalid.empty() ? "no @secret line" : parsed.invalid;
    return false;
  }
  Values stored = current(store_, app, parsed.spec);
  for (const auto& entry : values) {
    const std::string& key = entry.first;
    const std::string& value = entry.second;
    const Secret* secret = parsed.spec->find(key);
    const char* problem = nullptr;
    if (!secret) problem = "unknown key";
    else if (value.size() > kMaxValue) problem = "value too long";
    else if (!std::all_of(value.begin(), value.end(), script::headerValueChar)) problem = "invalid value";
    if (problem) {
      error = problem;
      return false;
    }
    stored.erase(std::remove_if(stored.begin(), stored.end(), [&key](const Stored& s) { return s.key == key; }),
                 stored.end());
    if (!value.empty()) stored.push_back({key, hostsOf(*secret), value});
  }
  if (save(store_, app, stored)) return true;
  error = "storage failed";
  return false;
}

bool Service::has(const std::string& app, const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  return find(current(store_, app, parsedLocked(app).spec), key) != nullptr;
}

bool Service::allows(const std::string& app, const std::string& key, const std::string& url) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Parsed& parsed = parsedLocked(app);
  return placeholdersInTarget(url) && !refusalFor(key, parsed.spec, current(store_, app, parsed.spec), url);
}

net::RequestFill Service::fill(const std::string& app, std::string& url, net::RequestHeaders& headers,
                               net::RequestBody& body) {
  const bool inUrl = url.find(kPlaceholder) != std::string::npos;
  const bool inBody = body.fillable();
  const auto marked = [](const auto& header) { return header.second.find(kPlaceholder) != std::string::npos; };
  if (!inUrl && !inBody && std::none_of(headers.begin(), headers.end(), marked)) return net::RequestFill::Unchanged;
  std::lock_guard<std::mutex> lock(mutex_);
  const Parsed& parsed = parsedLocked(app);
  const Values values = current(store_, app, parsed.spec);
  std::string filledUrl = url;
  net::RequestHeaders filled = headers;
  const char* reason =
      inUrl && !placeholdersInTarget(url) ? "host" : substitute(filledUrl, true, parsed.spec, values, url);
  for (auto header = filled.begin(); !reason && header != filled.end(); ++header)
    reason = substitute(header->second, false, parsed.spec, values, url);
  if (!reason && inBody) reason = substitute(body.head, false, parsed.spec, values, url);
  if (reason) {
    const std::string host = net::hostOf(url);
    logdbg("secret: %s refused (%s) for %s", app.c_str(), reason, host.empty() ? "?" : host.c_str());
    return net::RequestFill::Refused;
  }
  url = std::move(filledUrl);
  headers = std::move(filled);
  return net::RequestFill::Filled;
}

void Service::dropStale() {
  std::lock_guard<std::mutex> lock(mutex_);
  specs_.clear();
  std::vector<std::string> kept;
  store_.keepOnly([this, &kept](const std::string& json) {
    std::string app;
    Values values;
    if (!parse(json, app, values)) return false;
    const Parsed& parsed = parsedLocked(app);
    if (parsed.unread) return true;
    if (!parsed.spec) return false;
    kept.push_back(app);
    return true;
  });
  for (const std::string& app : kept) pruneLocked(app);
}

void Service::sourceSaved(const std::string& app) {
  std::lock_guard<std::mutex> lock(mutex_);
  specs_.erase(app);
  pruneLocked(app);
}

void Service::sourceRemoved(const std::string& app) {
  std::lock_guard<std::mutex> lock(mutex_);
  specs_.erase(app);
  store_.erase(app);
}

bool Service::eraseAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  specs_.clear();
  return store_.eraseAll();
}

const Service::Parsed& Service::parsedLocked(const std::string& app) {
  static const Parsed kNone, kUnread{std::nullopt, {}, true};
  const auto cached = specs_.find(app);
  if (cached != specs_.end()) return cached->second;
  const std::optional<std::string> source = source_(app);
  if (!source) return kUnread;
  if (source->empty()) return kNone;
  Parsed parsed;
  parsed.spec = parseSpec(*source, &parsed.invalid);
  return specs_.emplace(app, std::move(parsed)).first->second;
}

void Service::pruneLocked(const std::string& app) {
  const Parsed& parsed = parsedLocked(app);
  if (parsed.unread) return;
  const Values stored = loaded(store_, app);
  const Values kept = parsed.spec ? bound(*parsed.spec, stored) : Values{};
  if (kept.size() != stored.size()) save(store_, app, kept);
}

}
