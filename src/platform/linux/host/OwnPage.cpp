#include "platform/linux/host/OwnPage.h"

#include "core/api/ApiRouter.h"
#include "core/api/HttpProtocol.h"
#include "platform/linux/host/HostHttpBody.h"
#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::host {

bool fromOwnPage(const httplib::Request& req, const char* marker) {
  return req.get_header_value(marker) == "1" &&
         api::sameOrigin(req.get_header_value("Origin"), req.get_header_value("Host"),
                         req.get_header_value_count("Origin"), req.get_header_value_count("Host"), true);
}

bool refuseForeign(const httplib::Request& req, httplib::Response& res, const char* marker) {
  if (fromOwnPage(req, marker)) return false;
  const api::HttpResult refusal = api::forbiddenOrigin();
  host_http::sendJson(res, refusal.status, refusal.body);
  return true;
}

}
