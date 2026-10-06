#include "persistence/SystemConfigApi.h"
#include "persistence/SystemConfigApply.h"

namespace awtrix::api {
namespace {
HttpResult configuration(const DeviceConfig& config, bool secrets) {
  HttpResult result;
  result.body.reserve(1536);
  JsonWriter writer(result.body);
  writer.beginObject();
  config.write(writer, secrets);
  writer.endObject();
  return result;
}

}

HttpResult systemConfigRequest(const std::string& method, const std::string& body,
                               bool withSecrets, DeviceConfig& config,
                               const std::function<bool()>& commit) {
  if (method == "GET") return configuration(config, withSecrets);
  if (method != "PUT")
    return errorResult(405, "methodNotAllowed", "allowed: GET, PUT");
  if (!isWellFormed(body))
    return errorResult(400, "invalidJson", "invalid JSON");
  if (!JsonReader(body).isObject())
    return errorResult(422, "validationFailed", "expected an object");
  sysconfig::ApplyError failure;
  int applied = 0;
  if (!sysconfig::apply(config, JsonReader(body), applied, failure))
    return errorResult(failure.status, failure.code.c_str(), failure.message, failure.field);
  if (!commit())
    return errorResult(507, "insufficientStorage",
                 "applied, not saved yet");
  // Write responses never reveal secrets, even if the read option was requested.
  return configuration(config, false);
}

bool routeSystemConfig(const Request& request, Reply& reply, DeviceConfig& config,
                       const std::function<bool()>& commit) {
  if (request.path != "/api/v1/system") return false;
  std::string ignored;
  const bool secrets = request.query && request.query("secrets", ignored);
  reply.send(systemConfigRequest(request.method, request.body, secrets, config, commit));
  return true;
}

}
