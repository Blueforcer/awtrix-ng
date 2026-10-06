#pragma once

#include <functional>
#include "core/api/HttpExchange.h"
#include "persistence/DeviceConfig.h"

namespace awtrix::api {

// Called only after the transport's authentication/provisioning policy. The
// caller decides whether secrets may be shown. commit persists the accepted
// live configuration and notifies its platform; false means persistence pending.
HttpResult systemConfigRequest(const std::string& method, const std::string& body,
                               bool withSecrets, DeviceConfig& config,
                               const std::function<bool()>& commit);

bool routeSystemConfig(const Request& request, Reply& reply, DeviceConfig& config,
                       const std::function<bool()>& commit);

}
