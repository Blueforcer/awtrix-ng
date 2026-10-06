#include "platform/linux/oauth/OAuthVault.h"

#include <filesystem>
#include <system_error>
#include <vector>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/linux/oauth/OAuthFlow.h"
#include "platform/posix/Files.h"

namespace awtrix::oauth {
namespace {

constexpr std::size_t kMaxRecord = 32768;

bool read(const std::string& path, Record& out) {
  std::string json;
  if (!posix::readText(path, json, kMaxRecord) || !api::isWellFormed(json)) return false;
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

std::string Vault::path(const std::string& app) const {
  return dir_ + "/" + sha256Hex(app).substr(0, 32) + ".json";
}

bool Vault::load(const std::string& app, Record& out) const {
  Record r;
  if (!read(path(app), r) || r.app != app) return false;
  out = std::move(r);
  return true;
}

bool Vault::save(const std::string& app, const Record& record) {
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(dir_).parent_path(), error);
  if (!posix::ensurePrivateDirectory(dir_)) return false;
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
  return posix::replaceText(path(app), json);
}

bool Vault::erase(const std::string& app) {
  std::error_code error;
  if (!std::filesystem::remove(path(app), error)) return !error;
  return posix::fsyncDirectory(dir_);
}

bool Vault::eraseAll() {
  std::error_code error;
  std::filesystem::remove_all(dir_, error);
  return !error;
}

void Vault::keepOnly(const std::function<bool(const Record& record)>& keep) {
  std::error_code error;
  std::vector<std::filesystem::path> gone;
  std::filesystem::directory_iterator it(dir_, error), end;
  for (; !error && it != end; it.increment(error)) {
    Record r;
    if (!read(it->path().string(), r) || !keep(r)) gone.push_back(it->path());
  }
  for (const auto& p : gone) std::filesystem::remove(p, error);
  if (!gone.empty()) posix::fsyncDirectory(dir_);
}

}
