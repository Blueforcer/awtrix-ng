#include "platform/linux/Auth.h"
#include "platform/linux/LinuxLanLogin.h"
#include "platform/linux/host/vendor/httplib.h"
#include "persistence/DeviceConfig.h"
#include "core/payload/Base64.h"

#include <cstdio>
#include <cstdlib>

namespace {
void check(bool okay, const char* why) {
  if (!okay) { std::fprintf(stderr, "login: %s\n", why); std::exit(1); }
}
}

int main() {
  using namespace awtrix;
  LinuxLanLogin login;
  DeviceConfig config;
  config.authEnabled = true;
  config.authUser = "name";
  config.authPass = "secret";
  login.update(config);
  httplib::Request request;
  request.method = "GET";
  httplib::Response response;
  check(!login.admit(request, response) && response.status == 401 &&
        response.get_header_value("WWW-Authenticate") == "Basic realm=\"AWTRIX NG\"" &&
        response.get_header_value("Connection") == "close", "missing login is challenged");
  const auto valid = "Basic " + base64::encode("name:secret", 11);
  request.headers.emplace("Authorization", valid);
  check(login.admit(request, response), "valid Basic login");
  request.headers.emplace("Authorization", valid);
  check(!login.admit(request, response), "duplicate authorization refused");
  request.headers.clear();
  request.headers.emplace("Authorization", "Basic " + base64::encode("name:wrong", 10));
  check(!login.admit(request, response), "wrong credentials refused");
  request.method = "OPTIONS";
  check(login.admit(request, response), "CORS preflight stays accessible");
  config.authEnabled = false;
  login.update(config);
  request.method = "GET";
  check(login.admit(request, response), "disabled login stays accessible");
  std::string secret = "erase me";
  auth::wipe(secret);
  check(secret.empty() && auth::equal(auth::digest("a"), auth::digest("a")) &&
        !auth::equal(auth::digest("a"), auth::digest("b")), "secret helpers");
  httplib::Response admin;
  check(!auth::unauthorized(admin, "Basic realm=\"AWTRIX administration\", charset=\"UTF-8\"",
                            "administrator credentials required") && admin.status == 401 &&
        admin.body.find("administrator credentials required") != std::string::npos &&
        admin.get_header_value("WWW-Authenticate").find("UTF-8") != std::string::npos,
        "administration challenge keeps its own realm and message");
}
