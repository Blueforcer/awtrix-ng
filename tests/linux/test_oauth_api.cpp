#include "../support.h"
// The /api/v1/oauth routes.
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/api/JsonReader.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/oauth/OAuthApi.h"

using namespace awtrix;
namespace fs = std::filesystem;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

httplib::Request make(const char* method, const std::string& path, const std::string& body = "",
                      const char* origin = "http://192.168.1.50", bool marked = true) {
  httplib::Request r;
  r.method = method;
  r.path = path;
  r.body = body;
  r.headers.emplace("Host", "192.168.1.50");
  if (origin) r.headers.emplace("Origin", origin);
  if (marked) r.headers.emplace("X-Awtrix-OAuth", "1");
  return r;
}

}

int main() {
  const fs::path root = fs::temp_directory_path() / ("oauth-api-" + std::to_string(::getpid()));
  fs::remove_all(root);
  oauth::Vault vault((root / "p").string());
  std::map<std::string, std::string> sources = {
      {"Spot", "# @oauth authorize=https://a.test/auth token=https://a.test/token api=api.test pkce\n"},
      {"Plain", "class A end\n"}};
  oauth::Service service(
      vault, [](const oauth::HttpCall&) { return oauth::HttpReply{}; },
      [&](const std::string& a) {
        const auto it = sources.find(a);
        return it == sources.end() ? std::string() : it->second;
      },
      [] { return 0LL; });
  oauth::Api api(service, [&] {
    std::vector<std::string> names;
    for (const auto& entry : sources) names.push_back(entry.first);
    return names;
  });

  httplib::Response res;
  check(!api.handle(make("GET", "/api/v1/voice"), res), "other paths are not ours");
  check(!api.handle(make("GET", "/api/v1/oauthx"), res), "a longer word is not ours");

  res = {};
  check(api.handle(make("GET", "/api/v1/oauth"), res) && res.status == 200 &&
            res.body.find("\"name\":\"Spot\"") != std::string::npos &&
            res.body.find("\"name\":\"Plain\"") == std::string::npos &&
            res.body.find("https://awtrix.de/oauth/callback") != std::string::npos,
        "list shows only @oauth scripts");
  check(res.get_header_value("Cache-Control") == "no-store", "no-store");

  res = {};
  check(api.handle(make("GET", "/api/v1/oauth/Plain"), res) && res.status == 404, "404 without @oauth");
  res = {};
  check(api.handle(make("GET", "/api/v1/oauth/.."), res) && res.status == 404, "unsafe names refused");
  res = {};
  check(api.handle(make("GET", "/api/v1/oauth/Spot/other"), res) && res.status == 404, "unknown action");

  const std::string creds = R"({"clientId":"cid","clientSecret":"s3cret"})";
  res = {};
  check(api.handle(make("POST", "/api/v1/oauth/Spot", creds, "https://evil.example"), res) && res.status == 403,
        "foreign origin refused");
  res = {};
  check(api.handle(make("POST", "/api/v1/oauth/Spot", creds, "http://192.168.1.50", false), res) &&
            res.status == 403,
        "missing marker header refused");
  res = {};
  check(api.handle(make("POST", "/api/v1/oauth/Spot", creds, nullptr), res) && res.status == 403,
        "missing origin refused");
  for (const char* header : {"Origin", "Host"}) {
    auto duplicate = make("POST", "/api/v1/oauth/Spot", creds);
    duplicate.headers.emplace(header, header[0] == 'O' ? "http://192.168.1.50" : "192.168.1.50");
    res = {};
    check(api.handle(duplicate, res) && res.status == 403, "ambiguous authority refused");
  }
  res = {};
  check(api.handle(make("POST", "/api/v1/oauth/Spot", creds), res) && res.status == 200, "credentials saved");

  res = {};
  api.handle(make("GET", "/api/v1/oauth/Spot"), res);
  check(res.status == 200 && res.body.find("s3cret") == std::string::npos &&
            res.body.find("\"clientSecretSet\":true") != std::string::npos &&
            res.body.find("\"clientId\":\"cid\"") != std::string::npos,
        "secret is write-only");

  res = {};
  check(api.handle(make("POST", "/api/v1/oauth/Spot", R"({"clientId":7})"), res) && res.status == 422,
        "wrong types refused");
  for (const std::string& body : {std::string("not json"), std::string("[]"), std::string(8193, ' ')}) {
    res = {};
    check(api.handle(make("POST", "/api/v1/oauth/Spot", body), res) && res.status == 400, "bad body");
    const api::JsonReader error = api::memberValue(api::JsonReader(res.body), "error");
    std::string code;
    check(api::memberValue(error, "code").appendString(code) && code == "invalidJson",
          "bad JSON uses the shared API error code");
  }

  res = {};
  check(api.handle(make("POST", "/api/v1/oauth/Spot/start", "{}"), res) && res.status == 200 &&
            res.body.find("\"url\":\"https://a.test/auth?") != std::string::npos,
        "start returns the authorize url");

  res = {};
  check(api.handle(make("POST", "/api/v1/oauth/Spot/code", R"({"code":"x","state":"nope"})"), res) &&
            res.status == 422,
        "unknown state refused");
  res = {};
  check(api.handle(make("POST", "/api/v1/oauth/Spot/code", R"({"code":"x"})"), res) && res.status == 422,
        "state required");

  res = {};
  check(api.handle(make("PUT", "/api/v1/oauth/Spot"), res) && res.status == 405, "PUT not allowed");
  res = {};
  check(api.handle(make("DELETE", "/api/v1/oauth/Spot"), res) && res.status == 200, "sign out");
  res = {};
  api.handle(make("GET", "/api/v1/oauth/Spot"), res);
  check(res.body.find("\"clientId\":\"cid\"") != std::string::npos, "sign out keeps the client");

  fs::remove_all(root);
  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
