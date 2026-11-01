#include "platform/linux/oauth/OAuthVault.h"

#include <utility>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

namespace awtrix::oauth {
namespace {

constexpr std::size_t kMaxRecord = 32768;

bool parse(const std::string& json, Record& out) {
  if (!api::isWellFormed(json)) return false;
  const api::JsonReader r(json);
  if (!r.isObject()) return false;
  out = Record{};
  out.app = api::memberText(r, "app");
  out.binding = api::memberText(r, "binding");
  out.clientId = api::memberText(r, "clientId");
  out.clientSecret = api::memberText(r, "clientSecret");
  out.refreshToken = api::memberText(r, "refreshToken");
  out.accessToken = api::memberText(r, "accessToken");
  out.lastError = api::memberText(r, "lastError");
  return !out.app.empty();
}

}

Vault::Vault(std::string directory) : store_(std::move(directory), kMaxRecord) {}

bool Vault::load(const std::string& app, Record& out) const {
  std::string json;
  Record r;
  if (!store_.load(app, json) || !parse(json, r) || r.app != app) return false;
  out = std::move(r);
  return true;
}

bool Vault::save(const std::string& app, const Record& record) {
  std::string json;
  api::JsonWriter(json)
      .beginObject()
      .member("app", app)
      .member("binding", record.binding)
      .member("clientId", record.clientId)
      .member("clientSecret", record.clientSecret)
      .member("refreshToken", record.refreshToken)
      .member("accessToken", record.accessToken)
      .member("lastError", record.lastError)
      .endObject();
  return store_.save(app, json);
}

void Vault::keepOnly(const std::function<bool(const Record& record)>& keep) {
  store_.keepOnly([&keep](const std::string& json) {
    Record r;
    return parse(json, r) && keep(r);
  });
}

}
