#include "core/StrCase.h"
#include "platform/tc002/voice/VoiceConfig.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "core/net/Url.h"
#include "platform/posix/Files.h"

namespace awtrix::tc002::voice {
namespace {
bool safe(std::string_view text) {
  return std::all_of(text.begin(), text.end(), [](unsigned char c) {
    return c > 32 && c < 127 && c != '\\';
  });
}
bool stringField(const api::JsonReader& r, const char* name, std::string& value,
                 std::size_t limit) {
  const auto member = api::memberValue(r, name);
  if (!api::present(member)) return true;
  std::string next;
  if (!member.appendString(next) || next.size() > limit) return false;
  value = std::move(next);
  return true;
}
bool boolField(const api::JsonReader& r, const char* name, bool& value) {
  const auto member = api::memberValue(r, name);
  return !api::present(member) || member.asBool(value);
}
std::string serialize(const Config& c, bool secret) {
  std::string out;
  api::JsonWriter w(out);
  w.beginObject()
      .member("enabled", c.enabled)
      .member("url", c.url)
      .member("pipeline", c.pipeline)
      .member("device", c.device)
      .member("tokenSet", !c.token.empty());
  if (secret) w.member("token", c.token);
  w.endObject();
  return out;
}
// Returns the offending field, "" for the body as a whole, or nullptr when valid.
const char* applyConfig(std::string_view json, Config& c) {
  if (json.size() > 8192 || !api::isWellFormed(json)) return "";
  api::JsonReader r(json);
  if (!r.isObject()) return "";
  if (!boolField(r, "enabled", c.enabled)) return "enabled";
  if (!stringField(r, "url", c.url, 512)) return "url";
  if (!stringField(r, "token", c.token, 4096)) return "token";
  if (!stringField(r, "pipeline", c.pipeline, 128)) return "pipeline";
  if (!stringField(r, "device", c.device, 64) ||
      !std::all_of(c.device.begin(), c.device.end(),
                   [](unsigned char ch) { return std::isalnum(ch); }))
    return "device";
  bool clear = false;
  if (!boolField(r, "clearToken", clear)) return "clearToken";
  if (clear) c.token.clear();
  while (!c.url.empty() && c.url.back() == '/') c.url.pop_back();
  WebSocket::Endpoint endpoint;
  if (!c.url.empty() && !parseOrigin(c.url, endpoint)) return "url";
  if (!c.token.empty() && !safe(c.token)) return "token";
  if (!c.pipeline.empty() && !safe(c.pipeline)) return "pipeline";
  if (c.enabled && (c.url.empty() || c.token.empty())) return "enabled";
  return nullptr;
}
}  // namespace
bool parseOrigin(std::string_view url, WebSocket::Endpoint& out) {
  const auto parsed = net::parseUrl(url);
  if (!parsed || url.size() > 512 || !safe(url) || parsed->userinfo || !parsed->originOnly())
    return false;
  WebSocket::Endpoint e;
  e.tls = parsed->tls();
  auto host = parsed->host;
  if (host.front() == '[') host = host.substr(1, host.size() - 2);
  else if (host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") !=
           host.npos) return false;
  e.port = std::to_string(parsed->effectivePort);
  e.host = strcase::toLower(std::string(host));
  e.target = "/api/websocket";
  out = std::move(e);
  return true;
}
bool ttsTarget(const Config& config, std::string_view url,
               std::string& target) {
  if (url.empty() || url.size() > 4096 || !safe(url) ||
      url.find('#') != url.npos)
    return false;
  if (url.front() != '/') {
    const auto expected = net::parseUrl(config.url);
    const auto actual = net::parseUrl(url);
    if (!expected || !actual || expected->userinfo || actual->userinfo ||
        expected->effectivePort != actual->effectivePort || expected->scheme != actual->scheme)
      return false;
    if (!strcase::equalsIgnoreCase(expected->host, actual->host)) return false;
    url = actual->target;
  }
  // Only HA's TTS proxy; never fetch arbitrary administration paths with the
  // token.
  if (url.substr(0, 15) != "/api/tts_proxy/") return false;
  target.assign(url);
  return true;
}
ConfigStore::ConfigStore(std::string directory)
    : directory_(std::move(directory)) {}
bool ConfigStore::load() {
  if (!posix::ensurePrivateDirectory(directory_)) return false;
  const std::string path = directory_ + "/voice.json";
  struct stat st {};
  if (::lstat(path.c_str(), &st) != 0) return errno == ENOENT;
  if (!S_ISREG(st.st_mode) || st.st_uid != ::geteuid() || (st.st_mode & 0077))
    return false;
  std::string json;
  Config next;
  if (!posix::readText(path, json, 8192) || applyConfig(json, next))
    return false;
  config_ = std::move(next);
  return true;
}
bool ConfigStore::update(std::string_view json, Rejection& rejection) {
  Config next = config_;
  if (const char* field = applyConfig(json, next)) {
    rejection.field = field;
    rejection.message = *field == '\0' ? "expected an object"
                        : std::string_view(field) == "url"
                            ? "address only, no path"
                        : std::string_view(field) == "enabled"
                            ? "needs address and token"
                            : "invalid value";
    return false;
  }
  if (!config_.token.empty() &&
      !api::present(api::memberValue(api::JsonReader(json), "token")) &&
      !next.token.empty()) {
    WebSocket::Endpoint before, after;
    if (!parseOrigin(config_.url, before) || !parseOrigin(next.url, after) ||
        before.host != after.host || before.port != after.port ||
        before.tls != after.tls) {
      rejection.field = "token";
      rejection.message = "new address needs the token";
      return false;
    }
  }
  if (!posix::ensurePrivateDirectory(directory_) ||
      !posix::replaceText(directory_ + "/voice.json", serialize(next, true))) {
    rejection.stored = false;
    rejection.message = "could not save";
    return false;
  }
  config_ = std::move(next);
  return true;
}
std::string ConfigStore::publicJson() const {
  return serialize(config_, false);
}
bool ConfigStore::erase() {
  if (!posix::ensurePrivateDirectory(directory_)) return false;
  if (::unlink((directory_ + "/voice.json").c_str()) != 0 && errno != ENOENT)
    return false;
  config_ = {};
  return posix::fsyncDirectory(directory_);
}
}  // namespace awtrix::tc002::voice
