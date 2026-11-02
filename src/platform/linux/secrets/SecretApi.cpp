#include "platform/linux/secrets/SecretApi.h"

#include <string_view>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "persistence/ScriptFiles.h"
#include "platform/linux/host/ApiPath.h"
#include "platform/linux/host/HostHttpBody.h"
#include "platform/linux/host/OwnPage.h"
#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::secrets {
namespace {

constexpr std::string_view kPrefix = "/api/v1/secrets";
constexpr std::size_t kMaxBody = 16384;

using host_http::sendError;
using host_http::sendJson;

std::string statusJson(const Status& status) {
  std::string json;
  api::JsonWriter w(json);
  w.beginObject();
  if (!status.invalid.empty()) {
    w.member("invalid", status.invalid);
  } else {
    w.key("keys").beginArray();
    for (const KeyStatus& k : status.keys) {
      w.beginObject().member("key", k.key).member("label", k.label).member("help", k.help).member("set", k.set);
      w.key("hosts").beginArray();
      for (const std::string& host : k.hosts) w.value(host);
      w.endArray().endObject();
    }
    w.endArray();
  }
  w.endObject();
  return json;
}

bool stringMembers(const std::string& body, std::vector<std::pair<std::string, std::string>>& out) {
  api::JsonReader r(body);
  if (!r.enterObject()) return false;
  while (r.nextMember()) {
    const api::JsonReader value = r;
    std::string text;
    if (!value.isString() || !value.appendString(text)) return false;
    out.emplace_back(std::string(r.key()), std::move(text));
    if (!r.skipValue()) return false;
  }
  return true;
}

}

bool Api::handle(const httplib::Request& req, httplib::Response& res) {
  std::string name, key;
  if (!host::splitApiPath(req.path, kPrefix, name, key)) return false;
  res.set_header("Cache-Control", "no-store");

  if (name.empty()) {
    if (req.method != "GET") {
      sendError(res, 405, "methodNotAllowed", "allowed: GET");
      return true;
    }
    std::string json;
    api::JsonWriter w(json);
    w.beginObject().key("apps").beginArray();
    for (const std::string& app : scripts_())
      if (service_.status(app).declared) w.value(app);
    w.endArray().endObject();
    sendJson(res, 200, json);
    return true;
  }

  if (!scriptfiles::nameIsSafe(name) || key.find('/') != std::string::npos) {
    sendError(res, 404, "notFound", "unknown route");
    return true;
  }
  const Status current = service_.status(name);
  if (!current.declared) {
    sendError(res, 404, "notFound", "no @secret line");
    return true;
  }
  const bool whole = key.empty();
  if (whole && req.method == "GET") {
    sendJson(res, 200, statusJson(current));
    return true;
  }
  if (req.method != (whole ? "PUT" : "DELETE")) {
    sendError(res, 405, "methodNotAllowed", whole ? "allowed: GET, PUT" : "allowed: DELETE");
    return true;
  }
  if (host::refuseForeign(req, res, "X-Awtrix-Secret")) return true;

  std::vector<std::pair<std::string, std::string>> values;
  if (!whole) {
    values.emplace_back(key, std::string());
  } else if (req.body.size() > kMaxBody || !api::isWellFormed(req.body) || !api::JsonReader(req.body).isObject()) {
    sendError(res, 400, "invalidJson", "invalid JSON");
    return true;
  } else if (!stringMembers(req.body, values)) {
    sendError(res, 422, "validationFailed", "values must be strings");
    return true;
  }
  std::string error;
  if (!service_.set(name, values, error)) {
    sendError(res, 422, "validationFailed", error.c_str());
    return true;
  }
  sendJson(res, 200, "{\"ok\":true}");
  return true;
}

}
