#include "platform/tc002/runtime/Tc002Provisioning.h"
#include "platform/tc002/contract/tc002_layout.h"

#include "core/ProvisioningPolicy.h"
#include "core/api/ApiRouter.h"
#include "platform/linux/host/vendor/httplib.h"

namespace awtrix {

bool Tc002Provisioning::admit(const httplib::Request& request, httplib::Response& response) const {
  if (!active()) return true;
  const std::string host = TC002_AP_ADDRESS + (port_ == 80 ? std::string() : ":" + std::to_string(port_));
  const std::string origin = "http://" + host;
  const auto resolved = api::resolveHttpMethod(request.method, request.path,
      request.get_header_value(api::kMethodOverrideHeader));
  const bool hostValid = request.get_header_value_count("Host") == 1 &&
      provisioning::matchesAuthority(request.get_header_value("Host"), host, port_ == 80);
  const bool originValid = request.get_header_value_count("Origin") <= 1 &&
      (!request.has_header("Origin") ||
       provisioning::matchesAuthority(request.get_header_value("Origin"), origin, port_ == 80)) &&
      request.get_header_value_count("Sec-Fetch-Site") <= 1 &&
      request.get_header_value("Sec-Fetch-Site") != "cross-site";
  const auto verdict = provisioning::admit({resolved.method, request.path, request.body,
      hostValid, originValid, request.has_param("secrets"), resolved.error == nullptr});
  if (verdict == provisioning::Verdict::Allow) return true;
  response.set_header("Connection", "close");
  if (verdict == provisioning::Verdict::Redirect) {
    response.status = 302;
    response.set_header("Location", origin + "/");
  } else {
    response.status = 403;
    response.set_content(api::errorJson("forbidden", provisioning::kRefusal), "application/json");
  }
  return false;
}

}
