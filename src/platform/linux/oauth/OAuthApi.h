#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "platform/linux/oauth/OAuthService.h"

namespace httplib {
struct Request;
struct Response;
}

namespace awtrix::oauth {

// /api/v1/oauth: sign-in of scripts with an @oauth line. Changes come only from the device's own
// page, or behind a key login from whoever has the key.
class Api {
 public:
  Api(Service& service, std::function<std::vector<std::string>()> scripts, bool keyLogin = false)
      : service_(service), scripts_(std::move(scripts)), keyLogin_(keyLogin) {}
  // False when the path is not under /api/v1/oauth.
  bool handle(const httplib::Request& req, httplib::Response& res);

 private:
  Service& service_;
  std::function<std::vector<std::string>()> scripts_;
  bool keyLogin_;
};

}
