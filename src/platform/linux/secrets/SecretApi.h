#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "platform/linux/secrets/SecretService.h"

namespace httplib {
struct Request;
struct Response;
}

namespace awtrix::secrets {

class Api {
 public:
  Api(Service& service, std::function<std::vector<std::string>()> scripts, bool keyLogin = false)
      : service_(service), scripts_(std::move(scripts)), keyLogin_(keyLogin) {}
  bool handle(const httplib::Request& req, httplib::Response& res);

 private:
  Service& service_;
  std::function<std::vector<std::string>()> scripts_;
  bool keyLogin_;
};

}
