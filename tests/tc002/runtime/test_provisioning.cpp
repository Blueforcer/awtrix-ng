#include "../../support.h"
#include <cstdio>
#include <cstdlib>

#include "platform/tc002/runtime/Tc002Provisioning.h"
#include "core/api/JsonReader.h"
#include "platform/linux/host/vendor/httplib.h"

namespace {
constexpr auto check = awtrix::test::require;
httplib::Request request(const std::string& method, const std::string& path) {
  httplib::Request result;
  result.method = method;
  result.path = path;
  result.set_header("Host", "192.168.4.1");
  return result;
}
}

int main() {
  awtrix::Tc002Provisioning setup(80);
  for (const auto* path : {"/", "/index.html", "/api/v1/device", "/api/v1/system",
                           "/api/v1/capabilities", "/api/v1/system/wifi-scan"}) {
    auto req = request("GET", path);
    httplib::Response res;
    check(setup.admit(req, res), "setup page reads allowed");
  }
  for (const auto* body : {"{\"wifiSsid\":\"Home\",\"wifiPass\":\"password\"}",
                          "{\"wifiSsid\":\"Cafe\",\"wifiPass\":\"\"}", "{\"hostname\":\"kitchen\"}"}) {
    auto req = request("PUT", "/api/v1/system");
    req.body = body;
    httplib::Response res;
    check(setup.admit(req, res), "setup saves network fields");
  }
  for (const auto* body : {"{\"authEnabled\":false}", "{\"mqttPass\":\"x\"}",
                          "{\"wifiSsid\":\"A\",\"wifiSsid\":\"B\"}", "[]", "{broken", "{}"}) {
    auto req = request("PUT", "/api/v1/system");
    req.body = body;
    httplib::Response res;
    check(!setup.admit(req, res) && res.status == 403, "unrelated or malformed changes blocked");
  }
  for (const auto* path : {"/api/v1/files", "/api/v1/apps", "/wifi.json", "/system.json",
                           "/api/v1/logs", "/api/v1/scripts/demo/source"}) {
    auto req = request("GET", path);
    httplib::Response res;
    check(!setup.admit(req, res) && res.status == 403, "private data blocked");
  }
  for (const auto* path : {"/update", "/api/v1/files", "/api/v1/device/factory-reset"}) {
    auto req = request("POST", path);
    httplib::Response res;
    check(!setup.admit(req, res) && res.status == 403, "uploads and unrelated actions blocked");
  }
  {
    auto req = request("POST", "/api/v1/system");
    req.set_header("X-HTTP-Method-Override", "DELETE");
    httplib::Response res;
    check(!setup.admit(req, res), "method overrides cannot evade policy");
    req = request("GET", "/api/v1/system");
    req.params.emplace("secrets", "true");
    check(!setup.admit(req, res), "secret export blocked");
    req = request("PUT", "/api/v1/system");
    req.set_header("Origin", "https://unrelated.example");
    check(!setup.admit(req, res), "foreign web writes blocked");
  }
  {
    auto req = request("POST", "/api/v1/system");
    req.set_header("X-HTTP-Method-Override", "PUT");
    req.body = "{\"wifiSsid\":\"Home\"}";
    httplib::Response res;
    check(setup.admit(req, res), "Wi-Fi method override allowed");
    req.body = "{\"wifiSsid\":\"Home\",\"authEnabled\":false}";
    check(!setup.admit(req, res) && res.status == 403, "override still validates body");
    const auto error = awtrix::api::memberValue(awtrix::api::JsonReader(res.body), "error");
    std::string code;
    check(awtrix::api::memberValue(error, "code").appendString(code) && code == "forbidden",
          "policy refusal carries forbidden code");
  }
  {
    for (const auto* header : {"Origin", "Sec-Fetch-Site"}) {
      auto req = request("GET", "/api/v1/system");
      req.set_header(header, header == std::string("Origin") ? "http://192.168.4.1" : "same-origin");
      req.set_header(header, "invalid");
      httplib::Response res;
      check(!setup.admit(req, res) && res.status == 403, "ambiguous browser headers refused");
    }
    auto req = request("POST", "/api/v1/device/reboot");
    req.set_header("Host", "192.168.4.1");
    httplib::Response res;
    check(!setup.admit(req, res) && res.status == 403, "duplicate Host refused for writes");
    req = request("POST", "/api/v1/restore");
    req.body = "backup body";
    res = {};
    check(setup.admit(req, res), "backup restore available on the hotspot");
    req.set_header("Origin", "http://192.168.4.1");
    check(setup.admit(req, res), "same-origin backup restore available");
    req = request("POST", "/api/v1/restore");
    req.set_header("Origin", "https://unrelated.example");
    check(!setup.admit(req, res) && res.status == 403, "foreign-origin restore refused");
    req = request("POST", "/api/v1/restore");
    req.params.emplace("secrets", "1");
    res = {};
    check(!setup.admit(req, res) && res.status == 403, "restore with secrets refused");
    req = request("POST", "/api/v1/restore");
    req.headers.clear();
    req.set_header("Host", "foreign.example");
    res = {};
    check(!setup.admit(req, res) && res.status == 403 && !res.has_header("Location"),
          "foreign-host restore refused");
    for (const auto* method : {"GET", "PUT"}) {
      req = request(method, "/api/v1/restore");
      res = {};
      check(!setup.admit(req, res) && res.status == 403, "restore path takes only POST");
    }
    req = request("GET", "/api/v1/system");
    req.set_header("Sec-Fetch-Site", "cross-site");
    check(!setup.admit(req, res) && res.status == 403, "cross-site reads refused");
  }
  {
    auto req = request("GET", "/generate_204");
    req.headers.clear();
    req.set_header("Host", "connectivitycheck.example");
    httplib::Response res;
    check(!setup.admit(req, res) && res.status == 302 &&
          res.get_header_value("Location") == "http://192.168.4.1/", "captive probe redirected");
    req = request("POST", "/api/v1/device/reboot");
    req.headers.clear();
    req.set_header("Host", "foreign.example");
    res = {};
    check(!setup.admit(req, res) && res.status == 403 && !res.has_header("Location"),
          "foreign-host write refused, not redirected");
    req = request("GET", "/api/v1/system");
    req.headers.clear();
    req.set_header("Host", "192.168.4.1:80");
    req.set_header("Origin", "http://192.168.4.1:80");
    check(setup.admit(req, res), "explicit default port accepted");
    req = request("POST", "/api/v1/device/reboot");
    res = {};
    check(setup.admit(req, res), "reboot available");
    setup.setActive(false);
    req = request("POST", "/update");
    check(setup.admit(req, res), "station mode leaves normal authentication in charge");
  }
  std::puts("TC002 provisioning policy passed");
}
