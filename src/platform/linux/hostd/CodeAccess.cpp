#include "platform/linux/hostd/CodeAccess.h"

#include "core/api/HttpProtocol.h"

namespace awtrix::hostd {

bool showsCode(const CodeRequest& r, int port, bool haIngress) {
  if (r.peer == "127.0.0.1") {
    const std::string suffix = ":" + std::to_string(port);
    if (r.hostCount != 1 || (r.host != "127.0.0.1" + suffix && r.host != "localhost" + suffix)) return false;
  } else if (!haIngress || r.peer != kSupervisorAddress || r.ingressPathCount != 1 || r.ingressPath.empty()) {
    return false;
  }
  return !api::fromOtherSite({r.origin, r.originCount, r.host, r.hostCount, r.fetchSite, r.fetchSiteCount});
}

}
