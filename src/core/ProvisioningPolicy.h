#pragma once

#include <string_view>

#include "core/api/JsonReader.h"

namespace awtrix::provisioning {

enum class Verdict { Allow, Redirect, Refuse };

inline constexpr const char* kRefusal = "Wi-Fi setup only";

struct Request {
  std::string_view method;
  std::string_view path;
  std::string_view body;
  bool hostValid = false;
  bool originValid = false;
  bool secrets = false;
  bool methodValid = true;
};

inline bool matchesAuthority(std::string_view supplied, std::string_view expected,
                             bool defaultPort) {
  return supplied == expected ||
      (defaultPort && supplied.size() == expected.size() + 3 &&
       supplied.substr(0, expected.size()) == expected && supplied.substr(expected.size()) == ":80");
}

inline bool wifiBody(std::string_view body) {
  // The header guard runs before streaming requests have a body; the route guard runs again after.
  if (body.empty()) return true;
  if (body.size() > 1024) return false;
  api::JsonReader json(body);
  if (!json.enterObject()) return false;
  unsigned seen = 0;
  while (json.nextMember()) {
    const unsigned field = json.keyEquals("wifiSsid") ? 1 : json.keyEquals("wifiPass") ? 2 :
                           json.keyEquals("hostname") ? 4 : 0;
    if (!field || (seen & field) || !json.isString()) return false;
    seen |= field;
    if (!json.skipValue()) return false;
  }
  return json.ok() && json.atEnd() && seen;
}

inline Verdict admit(const Request& request) {
  if (!request.hostValid)
    return request.method == "GET" || request.method == "HEAD" ? Verdict::Redirect : Verdict::Refuse;
  if (!request.originValid || request.secrets || !request.methodValid) return Verdict::Refuse;
  if (request.method == "GET") {
    const auto path = request.path;
    if (path == "/" || path == "/index.html" || path == "/api/v1/device" ||
        path == "/api/v1/capabilities" || path == "/api/v1/system" ||
        path == "/api/v1/system/wifi-scan") return Verdict::Allow;
    if (path.substr(0, 5) != "/api/" && path.find('.', 1) == path.npos) return Verdict::Redirect;
    return Verdict::Refuse;
  }
  if (request.method == "PUT" && request.path == "/api/v1/system" && wifiBody(request.body))
    return Verdict::Allow;
  if (request.method == "POST" &&
      (request.path == "/api/v1/device/reboot" || request.path == "/api/v1/restore"))
    return Verdict::Allow;
  return Verdict::Refuse;
}

}
