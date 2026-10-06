#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "core/Command.h"

namespace awtrix {
class CoreEngine;
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

// The {name} of /api/v1/apps/{name}<suffix>, empty when the path does not end in the suffix.
std::string appSubresourceName(const std::string& path, std::string_view suffix);
// The {name} of /api/v1/apps/{name}/config, or of /api/v1/apps/builtin/{name}/config with
// builtin set.
std::string appConfigName(const std::string& path, bool& builtin);

bool isRawBodyWrite(const std::string& method, const std::string& path);

// A missing or empty Content-Type is accepted. JSON writes otherwise need application/json
// (case-insensitive); raw script source is exempt.
bool acceptsBodyContentType(const std::string& method, const std::string& path,
                            const std::string& contentType);

// Moves payload into cmd only when it returns Routed.
RouteOutcome routeMqtt(const std::string& suffix, std::string& payload, Command& cmd,
                       std::string& resultPayload);

bool isResultEcho(const std::string& suffix);

std::string errorJson(const char* code, const std::string& message,
                      const std::string& field = "");

HttpResult errorResult(int status, const char* code, const std::string& message,
                       const std::string& field = "");
HttpResult errorResult(int status, const char* code, const char* message,
                       const char* field = "");

inline constexpr const char* kAuthenticationChallenge = "Basic realm=\"AWTRIX NG\"";
HttpResult unauthorized(const char* message = "authentication required");

// Repeated headers are rejected; callers choose whether a missing Origin is permitted.
bool sameOrigin(std::string_view origin, std::string_view host,
                std::size_t originCount, std::size_t hostCount, bool required = false);

HttpResult httpResponse(const Command& cmd, DispatchResult r, const DispatchDetail& detail);
HttpResult commandResponse(CoreEngine& engine, const Command& cmd, DispatchResult result,
                           bool persistencePending = false);

std::string mqttResult(DispatchResult r, const DispatchDetail& detail);

// The <prefix>/event/error message for a rejected request, built from the error body the sender
// got back; empty when that body is not an error.
std::string errorEvent(const char* source, const std::string& request, const std::string& body);

}
}
