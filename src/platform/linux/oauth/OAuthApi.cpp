#include "platform/linux/oauth/OAuthApi.h"

#include <optional>
#include <string_view>

#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "persistence/ScriptFiles.h"
#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::oauth {
namespace {

constexpr std::string_view kPrefix = "/api/v1/oauth";
constexpr std::size_t kMaxBody = 8192;
constexpr std::size_t kMaxField = 4096;

void sendJson(httplib::Response& res, int status, const std::string& json) {
  res.status = status;
  res.set_content(json, "application/json");
}

void sendError(httplib::Response& res, int status, const char* code, const std::string& message) {
  sendJson(res, status, api::errorJson(code, message));
}

void writeStatus(api::JsonWriter& w, const std::string& name, const Status& s) {
  w.beginObject()
      .member("name", name)
      .member("provider", s.provider)
      .member("scope", s.scope)
      .member("pkce", s.pkce)
      .member("clientId", s.clientId)
      .member("clientSecretSet", s.clientSecretSet)
      .member("state", s.state);
  if (!s.error.empty()) w.member("error", s.error);
  if (!s.invalid.empty()) w.member("invalid", s.invalid);
  w.endObject();
}

bool fromOwnPage(const httplib::Request& req) {
  return req.get_header_value("X-Awtrix-OAuth") == "1" &&
         api::sameOrigin(req.get_header_value("Origin"), req.get_header_value("Host"),
                         req.get_header_value_count("Origin"), req.get_header_value_count("Host"), true);
}

// A string member: nullopt when absent; false when present but not a string within limit.
bool stringMember(const api::JsonReader& body, const char* name, std::optional<std::string>& out) {
  const auto m = api::memberValue(body, name);
  if (!api::present(m)) return true;
  std::string value;
  if (!m.isString() || !m.appendString(value) || value.size() > kMaxField) return false;
  out = std::move(value);
  return true;
}

}

bool Api::handle(const httplib::Request& req, httplib::Response& res) {
  const std::string& path = req.path;
  if (path.compare(0, kPrefix.size(), kPrefix) != 0 || (path.size() > kPrefix.size() && path[kPrefix.size()] != '/'))
    return false;
  res.set_header("Cache-Control", "no-store");

  std::string name, action;
  if (path.size() > kPrefix.size() + 1) {
    const std::string rest = path.substr(kPrefix.size() + 1);
    const std::size_t slash = rest.find('/');
    name = rest.substr(0, slash);
    if (slash != std::string::npos) action = rest.substr(slash + 1);
  }

  if (name.empty()) {
    if (req.method != "GET") {
      sendError(res, 405, "methodNotAllowed", "allowed: GET");
      return true;
    }
    std::string json;
    api::JsonWriter w(json);
    w.beginObject().member("redirectUri", kRedirectUri).key("apps").beginArray();
    for (const std::string& app : scripts_()) {
      const Status s = service_.status(app);
      if (s.declared) writeStatus(w, app, s);
    }
    w.endArray().endObject();
    sendJson(res, 200, json);
    return true;
  }

  if (!scriptfiles::nameIsSafe(name) || (action != "" && action != "start" && action != "code")) {
    sendError(res, 404, "notFound", "unknown route");
    return true;
  }
  const Status current = service_.status(name);
  if (!current.declared) {
    sendError(res, 404, "notFound", "no @oauth line");
    return true;
  }
  if (req.method == "GET" && action.empty()) {
    std::string json;
    api::JsonWriter w(json);
    writeStatus(w, name, current);
    sendJson(res, 200, json);
    return true;
  }
  const bool post = req.method == "POST", remove = req.method == "DELETE" && action.empty();
  if (!post && !remove) {
    sendError(res, 405, "methodNotAllowed", action.empty() ? "allowed: GET, POST, DELETE" : "allowed: POST");
    return true;
  }
  if (!fromOwnPage(req)) {
    sendError(res, 403, "forbiddenOrigin", "only from this device's web page");
    return true;
  }
  if (remove) {
    service_.signOut(name);
    sendJson(res, 200, "{\"ok\":true}");
    return true;
  }

  const std::string body = req.body.empty() ? std::string("{}") : req.body;
  if (body.size() > kMaxBody || !api::isWellFormed(body) || !api::JsonReader(body).isObject()) {
    sendError(res, 400, "invalidJson", "invalid JSON");
    return true;
  }
  const api::JsonReader json(body);
  std::string error;
  bool ok = false;
  if (action.empty()) {
    std::optional<std::string> clientId, clientSecret;
    bool clear = false;
    const auto clearMember = api::memberValue(json, "clearSecret");
    if (!stringMember(json, "clientId", clientId) || !stringMember(json, "clientSecret", clientSecret) ||
        (api::present(clearMember) && !clearMember.asBool(clear))) {
      sendError(res, 422, "validationFailed", "invalid field");
      return true;
    }
    ok = service_.saveClient(name, clientId, clientSecret, clear, error);
    if (ok) sendJson(res, 200, "{\"ok\":true}");
  } else if (action == "start") {
    std::string url;
    ok = service_.start(name, req.get_header_value("Origin"), url, error);
    if (ok) {
      std::string out;
      api::JsonWriter(out).beginObject().member("url", url).endObject();
      sendJson(res, 200, out);
    }
  } else {
    std::optional<std::string> code, state;
    if (!stringMember(json, "code", code) || !stringMember(json, "state", state) || !code || !state) {
      sendError(res, 422, "validationFailed", "code and state required");
      return true;
    }
    ok = service_.submitCode(name, *code, *state, error);
    if (ok) sendJson(res, 202, "{\"ok\":true}");
  }
  if (!ok) sendError(res, 422, "validationFailed", error);
  return true;
}

}
