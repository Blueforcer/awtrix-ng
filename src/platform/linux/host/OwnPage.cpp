#include "platform/linux/host/OwnPage.h"

#include "core/api/ApiRouter.h"
#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::host {

bool fromOwnPage(const httplib::Request& req, const char* marker) {
  return req.get_header_value(marker) == "1" &&
         api::sameOrigin(req.get_header_value("Origin"), req.get_header_value("Host"),
                         req.get_header_value_count("Origin"), req.get_header_value_count("Host"), true);
}

}
