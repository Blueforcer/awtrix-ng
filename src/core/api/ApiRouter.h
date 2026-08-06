#pragma once

#include <string>

#include "core/Command.h"

namespace awtrix {
namespace api {

struct HttpResult {
  int status = 200;
  const char* contentType = "application/json";
  std::string body;
  int retryAfterSeconds = 0;
};

enum class RouteOutcome : uint8_t {
  NoMatch,
  Routed,
  Respond,
};

RouteOutcome routeHttp(const std::string& method, const std::string& path,
                       std::string&& body, Command& cmd, HttpResult& immediate);

inline constexpr const char* kMethodOverrideHeader = "X-HTTP-Method-Override";

struct MethodResolution {
  std::string method;
  const char* error = nullptr;
};

MethodResolution resolveHttpMethod(const std::string& method, const std::string& path,
                                   const std::string& requested);

bool isValidAppName(const std::string& name);

std::string configAppName(const std::string& path);

bool isRawBodyWrite(const std::string& method, const std::string& path);

RouteOutcome routeMqtt(const std::string& suffix, const std::string& payload,
                       Command& cmd, std::string& resultPayload);

bool isResultEcho(const std::string& suffix);

std::string errorJson(const char* code, const std::string& message,
                      const std::string& field = "");

HttpResult httpResponse(const Command& cmd, DispatchResult r, const DispatchDetail& detail);

std::string mqttResult(DispatchResult r, const DispatchDetail& detail);

}
}
