#include "core/api/ApiRouter.h"

#include <string>
#include <string_view>

#include "core/api/JsonReader.h"
#include "core/api/JsonText.h"
#include "core/api/ScriptSoundsApi.h"
#include "core/sound/AudioRouter.h"
#include "core/sound/SoundMp3.h"
#include "core/sound/SoundSpec.h"

namespace awtrix {
namespace api {

namespace {

Command make(CommandType t, Source src) {
  Command c(t);
  c.source = src;
  return c;
}

std::string tailAfter(const std::string& path, const std::string& prefix) {
  if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) return "";
  return path.substr(prefix.size());
}

int indicatorId(const std::string& tail) {
  if (tail.size() != 1 || tail[0] < '1' || tail[0] > '3') return 0;
  return tail[0] - '0';
}

std::string mqttError(const char* code, const std::string& message, const std::string& field) {
  std::string out = "{\"ok\":false,";
  const std::string err = errorJson(code, message, field);
  out.append(err, 1, std::string::npos);
  return out;
}

// Reuses a ready-made HTTP error body by swapping its opening brace for the MQTT ok flag.
std::string mqttError(const std::string& httpBody) {
  std::string out = "{\"ok\":false,";
  out.append(httpBody, 1, std::string::npos);
  return out;
}

// The body is one sound object and travels as sent: the dispatcher reads it, and every transport
// runs it at once, so a mistake in it is still the answer to this request.
RouteOutcome routeAudioPlay(std::string& body, Source src, Command& cmd,
                            HttpResult& immediate) {
  if (!isWellFormed(body)) {
    immediate = errorResult(400, "invalidJson", "invalid JSON");
    return RouteOutcome::Respond;
  }
  cmd = make(CommandType::PlayAudio, src);
  cmd.arg = static_cast<int>(sound::PlayAs::Once);
  cmd.payload = std::move(body);
  return RouteOutcome::Routed;
}

RouteOutcome routeAudioStop(const std::string& body, Source src, Command& cmd,
                            HttpResult& immediate) {
  cmd = make(CommandType::StopAudio, src);
  cmd.arg = static_cast<int>(sound::Stop::All);
  if (body.empty()) return RouteOutcome::Routed;
  if (!isWellFormed(body)) {
    immediate = errorResult(400, "invalidJson", "invalid JSON");
    return RouteOutcome::Respond;
  }
  JsonReader it{std::string_view(body)};
  if (!it.enterObject()) {
    immediate = errorResult(422, "validationFailed", "must be an object");
    return RouteOutcome::Respond;
  }
  while (it.nextMember()) {
    if (!it.keyEquals("group")) {
      immediate = errorResult(422, "validationFailed", "unknown field", std::string(it.key()));
      return RouteOutcome::Respond;
    }
    std::string group;
    if (it.isString()) it.appendString(group);
    if (group == "alert") {
      cmd.arg = static_cast<int>(sound::Stop::Alert);
    } else if (group == "app") {
      cmd.arg = static_cast<int>(sound::Stop::App);
    } else if (group == "radio") {
      cmd.arg = static_cast<int>(sound::Stop::Radio);
    } else {
      immediate = errorResult(422, "validationFailed", "must be alert, app or radio", "group");
      return RouteOutcome::Respond;
    }
    if (!it.skipValue()) break;
  }
  return RouteOutcome::Routed;
}

}

// Script and sound-folder names share one rule; fixed routes under /api/v1/apps/ are reserved.
bool isValidAppName(const std::string& name) {
  for (std::string_view route : {"active", "next", "previous", "order"})
    if (name == route) return false;
  return sound::validName(name);
}

std::string appSubresourceName(const std::string& path, std::string_view suffix) {
  const std::string tail = tailAfter(path, "/api/v1/apps/");
  if (tail.size() <= suffix.size()) return {};
  if (tail.compare(tail.size() - suffix.size(), suffix.size(), suffix) != 0) return {};
  return tail.substr(0, tail.size() - suffix.size());
}

std::string appConfigName(const std::string& path, bool& builtin) {
  std::string name = appSubresourceName(path, "/config");
  builtin = name.rfind("builtin/", 0) == 0;
  if (builtin) name.erase(0, sizeof("builtin/") - 1);
  return name;
}

// Script uploads carry Berry source, not JSON, so the server has to pass the body through raw.
// A script's sounds share the prefix but are files, not source.
bool isRawBodyWrite(const std::string& method, const std::string& path) {
  if (method != "PUT") return false;
  if (!tailAfter(path, "/api/v1/apps/script-update/").empty()) return true;
  return !tailAfter(path, "/api/v1/apps/script/").empty() && !scriptsounds::match(path).matched;
}

bool acceptsBodyContentType(const std::string& method, const std::string& path,
                            const std::string& contentType) {
  if ((method != "PUT" && method != "PATCH") || isRawBodyWrite(method, path)) return true;
  if (contentType.empty()) return true;
  if (contentType.find_first_of("\r\n") != std::string::npos ||
      contentType.find('\0') != std::string::npos) return false;
  const auto start = contentType.find_first_not_of(" \t");
  const auto semicolon = contentType.find(';');
  const auto end = contentType.find_last_not_of(" \t", semicolon == std::string::npos
      ? std::string::npos : (semicolon == 0 ? 0 : semicolon - 1));
  if (start == std::string::npos || end == std::string::npos || end < start) return false;
  std::string token = contentType.substr(start, end - start + 1);
  for (char& c : token) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return token == "application/json";
}

MethodResolution resolveHttpMethod(const std::string& method, const std::string& path,
                                   const std::string& requested) {
  MethodResolution out;
  out.method = method;

  const std::size_t first = requested.find_first_not_of(" \t");
  if (first == std::string::npos) return out;
  const std::size_t last = requested.find_last_not_of(" \t");
  std::string want = requested.substr(first, last - first + 1);
  for (char& c : want)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');

  if (method != "POST") {
    out.error = "method override needs POST";
    return out;
  }
  if (want != "PUT" && want != "PATCH" && want != "DELETE") {
    out.error = "expected PUT, PATCH or DELETE";
    return out;
  }
  if (isRawBodyWrite(want, path)) {
    out.error = "no method override for scripts";
    return out;
  }
  out.method = std::move(want);
  return out;
}

// Turns a request into a Command, or into an immediate error response. NoMatch means the path is
// served elsewhere - notably every GET, since reads are answered directly, not as commands.
RouteOutcome routeHttp(const std::string& method, const std::string& path,
                       std::string&& body, Command& cmd, HttpResult& immediate) {
  const Source src = Source::Http;
  const bool post = method == "POST";
  const bool put = method == "PUT";
  const bool patch = method == "PATCH";
  const bool del = method == "DELETE";
  const bool get = method == "GET";

  auto command = [&](CommandType t) {
    cmd = make(t, src);
    cmd.payload = std::move(body);
    return RouteOutcome::Routed;
  };
  auto methodNotAllowed = [&](const char* allowed) {
    immediate = errorResult(405, "methodNotAllowed",
                            std::string("allowed: ") + allowed);
    return RouteOutcome::Respond;
  };
  auto requireBody = [&](const char* message) {
    immediate = errorResult(422, "validationFailed", message);
    return RouteOutcome::Respond;
  };

  if (path == "/api/v1/notifications") {
    if (post) return command(CommandType::Notify);
    return methodNotAllowed("POST");
  }
  if (path == "/api/v1/notifications/active") {
    if (del) return command(CommandType::DismissNotify);
    return methodNotAllowed("DELETE");
  }
  {
    const std::string name = tailAfter(path, "/api/v1/notifications/");
    if (!name.empty()) {
      if (!del) return methodNotAllowed("DELETE");
      cmd = make(CommandType::DismissNotify, src);
      cmd.name = name;
      return RouteOutcome::Routed;
    }
  }

  if (path == "/api/v1/apps") {
    if (get) return RouteOutcome::NoMatch;
    return methodNotAllowed("GET");
  }
  if (path == "/api/v1/apps/active") {
    if (put) return command(CommandType::SwitchApp);
    return methodNotAllowed("PUT");
  }
  if (path == "/api/v1/apps/next") {
    if (post) return command(CommandType::NextApp);
    return methodNotAllowed("POST");
  }
  if (path == "/api/v1/apps/previous") {
    if (post) return command(CommandType::PreviousApp);
    return methodNotAllowed("POST");
  }
  if (path == "/api/v1/apps/order") {
    if (put) return command(CommandType::SetAppOrder);
    return methodNotAllowed("PUT");
  }

  auto badName = [&]() {
    immediate = errorResult(400, "invalidName", "invalid name", "name");
    return RouteOutcome::Respond;
  };

  {
    const std::string name = tailAfter(path, "/api/v1/apps/pushed/");
    if (!name.empty()) {
      if (!put) return methodNotAllowed("PUT");
      if (!isValidAppName(name)) return badName();
      if (isEmptyObject(body)) return requireBody("body required");
      cmd = make(CommandType::SetPushedApp, src);
      cmd.name = name;
      cmd.payload = std::move(body);
      return RouteOutcome::Routed;
    }
  }

  if (path == "/api/v1/scripts/shared") {
    if (get) return RouteOutcome::NoMatch;
    return methodNotAllowed("GET");
  }

  // A script's sounds are files, served by the transport like the other uploads.
  if (scriptsounds::match(path).matched) return RouteOutcome::NoMatch;

  {
    const std::string updateName = tailAfter(path, "/api/v1/apps/script-update/");
    if (!updateName.empty()) {
      if (!put) return methodNotAllowed("PUT");
      if (!isValidAppName(updateName)) return badName();
      cmd = make(CommandType::ScriptUpdate, src);
      cmd.name = updateName;
      cmd.payload = std::move(body);
      return RouteOutcome::Routed;
    }
    const std::string name = tailAfter(path, "/api/v1/apps/script/");
    if (!name.empty()) {
      if (get) return RouteOutcome::NoMatch;
      if (!put) return methodNotAllowed("GET, PUT");
      if (!isValidAppName(name)) return badName();
      if (body.empty()) {
        immediate = errorResult(422, "validationFailed", "body must be the script",
                                "source");
        return RouteOutcome::Respond;
      }
      cmd = make(CommandType::ScriptSet, src);
      cmd.name = name;
      cmd.payload = std::move(body);
      return RouteOutcome::Routed;
    }
  }

  {
    const std::string name = appSubresourceName(path, "/enabled");
    if (!name.empty()) {
      if (!put) return methodNotAllowed("PUT");
      if (!isValidAppName(name)) return badName();
      cmd = make(CommandType::SetAppEnabled, src);
      cmd.name = name;
      cmd.payload = std::move(body);
      return RouteOutcome::Routed;
    }
  }

  {
    struct Subresource {
      std::string_view suffix;
      CommandType type;
      const char* hint;
    };
    static constexpr Subresource kSubresources[] = {
        {"/config", CommandType::ScriptConfigSet, "body required"},
        {"/data", CommandType::ScriptDataSet, "body required"},
    };
    for (const Subresource& sub : kSubresources) {
      bool builtin = false;
      const std::string name = sub.suffix == "/config" ? appConfigName(path, builtin) :
                                                        appSubresourceName(path, sub.suffix);
      if (name.empty() && !builtin) continue;
      if (get) return RouteOutcome::NoMatch;
      if (!patch) return methodNotAllowed("GET, PATCH");
      if (!isValidAppName(name)) return badName();
      if (body.empty()) return requireBody(sub.hint);
      cmd = make(builtin ? CommandType::BuiltinAppConfigSet : sub.type, src);
      cmd.name = name;
      cmd.payload = std::move(body);
      return RouteOutcome::Routed;
    }
  }

  // Reached only after the pushed/, script/, /config and /data routes above, so any leftover slash
  // simply fails the name check.
  {
    const std::string name = tailAfter(path, "/api/v1/apps/");
    if (!name.empty()) {
      if (!isValidAppName(name)) return badName();
      if (!del) return methodNotAllowed("DELETE");
      cmd = make(CommandType::DeleteApp, src);
      cmd.name = name;
      cmd.clear = true;
      return RouteOutcome::Routed;
    }
  }

  if (path == "/api/v1/settings") {
    if (patch) return command(CommandType::SetSettings);
    if (get) return RouteOutcome::NoMatch;
    return methodNotAllowed("GET, PATCH");
  }
  if (path == "/api/v1/settings/reset") {
    if (post) return command(CommandType::ResetSettings);
    return methodNotAllowed("POST");
  }

  if (path == "/api/v1/display") {
    if (patch) return command(CommandType::SetDisplay);
    if (get) return RouteOutcome::NoMatch;
    return methodNotAllowed("GET, PATCH");
  }
  if (path == "/api/v1/display/moodlight") {
    if (put) {
      if (isEmptyObject(body)) return requireBody("body required");
      return command(CommandType::Moodlight);
    }
    if (del) {
      cmd = make(CommandType::Moodlight, src);
      cmd.clear = true;
      return RouteOutcome::Routed;
    }
    return methodNotAllowed("PUT, DELETE");
  }

  {
    const std::string tail = tailAfter(path, "/api/v1/indicators/");
    if (!tail.empty()) {
      const int id = indicatorId(tail);
      if (id == 0) {
        immediate = errorResult(404, "notFound", "id must be 1..3");
        return RouteOutcome::Respond;
      }
      if (put) {
        if (isEmptyObject(body)) return requireBody("body required");
        cmd = make(CommandType::SetIndicator, src);
        cmd.arg = id;
        cmd.payload = std::move(body);
        return RouteOutcome::Routed;
      }
      if (del) {
        cmd = make(CommandType::SetIndicator, src);
        cmd.arg = id;
        cmd.clear = true;
        return RouteOutcome::Routed;
      }
      return methodNotAllowed("PUT, DELETE");
    }
  }

  if (path == "/api/v1/audio/play") {
    if (post) {
      if (body.empty()) return requireBody("body required");
      return routeAudioPlay(body, src, cmd, immediate);
    }
    return methodNotAllowed("POST");
  }

  // Stops everything the output is doing, stream included, unless a group is named.
  if (path == "/api/v1/audio/stop") {
    if (post) return routeAudioStop(body, src, cmd, immediate);
    return methodNotAllowed("POST");
  }

  if (path == "/api/v1/audio/stations") {
    if (put) {
      if (body.empty()) return requireBody("body required");
      return command(CommandType::SetRadioStations);
    }
    if (get) return RouteOutcome::NoMatch;
    return methodNotAllowed("GET, PUT");
  }

  if (path == "/api/v1/device/reboot") {
    if (post) return command(CommandType::Reboot);
    return methodNotAllowed("POST");
  }
  if (path == "/api/v1/device/sleep") {
    if (post) return command(CommandType::Sleep);
    return methodNotAllowed("POST");
  }
  if (path == "/api/v1/device/factory-reset") {
    if (post) return command(CommandType::FactoryReset);
    return methodNotAllowed("POST");
  }

  if (path == "/api/v1/audio" || path == "/api/v1/device" ||
      path == "/api/v1/display/screen" ||
      path == "/api/v1/capabilities" ||
      path == "/api/v1/version" || path == "/version" ||
      path == "/api/v1/system/wifi-scan" || path == "/api/v1/logs") {
    if (get) return RouteOutcome::NoMatch;
    return methodNotAllowed("GET");
  }

  return RouteOutcome::NoMatch;
}

// MQTT has no verbs, so an empty payload stands for the DELETE form of a command and shows up as
// cmd.clear.
RouteOutcome routeMqtt(const std::string& suffix, std::string& payload, Command& cmd,
                       std::string& resultPayload) {
  const Source src = Source::Mqtt;
  auto command = [&](CommandType t) {
    cmd = make(t, src);
    cmd.payload = std::move(payload);
    return RouteOutcome::Routed;
  };

  const std::string op = tailAfter(suffix, "cmd/");
  if (op.empty()) return RouteOutcome::NoMatch;

  if (op == "notify") return command(CommandType::Notify);
  if (op == "notify/dismiss") return command(CommandType::DismissNotify);
  {
    const std::string name = tailAfter(op, "notify/dismiss/");
    if (!name.empty()) {
      cmd = make(CommandType::DismissNotify, src);
      cmd.name = name;
      return RouteOutcome::Routed;
    }
  }
  {
    const std::string name = tailAfter(op, "apps/pushed/");
    if (!name.empty()) {
      if (!isValidAppName(name)) {
        resultPayload = mqttError("invalidName", "invalid name", "name");
        return RouteOutcome::Respond;
      }
      cmd = make(CommandType::SetPushedApp, src);
      cmd.name = name;
      cmd.clear = payload.empty();
      cmd.payload = std::move(payload);
      return RouteOutcome::Routed;
    }
  }
  {
    const std::string name = appSubresourceName("/api/v1/" + op, "/enabled");
    if (!name.empty()) {
      if (!isValidAppName(name)) {
        resultPayload = mqttError("invalidName", "invalid name", "name");
        return RouteOutcome::Respond;
      }
      cmd = make(CommandType::SetAppEnabled, src);
      cmd.name = name;
      cmd.payload = std::move(payload);
      return RouteOutcome::Routed;
    }
  }
  if (op == "apps/switch") return command(CommandType::SwitchApp);
  if (op == "apps/next") return command(CommandType::NextApp);
  if (op == "apps/previous") return command(CommandType::PreviousApp);
  if (op == "apps/order") return command(CommandType::SetAppOrder);
  if (op == "audio/stations") return command(CommandType::SetRadioStations);
  if (op == "settings") return command(CommandType::SetSettings);
  if (op == "settings/reset") return command(CommandType::ResetSettings);
  if (op == "display") return command(CommandType::SetDisplay);
  if (op == "display/moodlight") {
    cmd = make(CommandType::Moodlight, src);
    cmd.clear = payload.empty();
    cmd.payload = std::move(payload);
    return RouteOutcome::Routed;
  }
  {
    const std::string tail = tailAfter(op, "indicators/");
    if (!tail.empty()) {
      const int id = indicatorId(tail);
      if (id == 0) return RouteOutcome::NoMatch;
      cmd = make(CommandType::SetIndicator, src);
      cmd.arg = id;
      cmd.clear = payload.empty();
      cmd.payload = std::move(payload);
      return RouteOutcome::Routed;
    }
  }
  if (op == "audio/play") {
    HttpResult imm;
    const RouteOutcome o = routeAudioPlay(payload, src, cmd, imm);
    if (o == RouteOutcome::Respond) resultPayload = mqttError(imm.body);
    return o;
  }
  if (op == "audio/stop") {
    HttpResult imm;
    const RouteOutcome o = routeAudioStop(payload, src, cmd, imm);
    if (o == RouteOutcome::Respond) resultPayload = mqttError(imm.body);
    return o;
  }
  if (op == "device/reboot") return command(CommandType::Reboot);
  if (op == "device/sleep") return command(CommandType::Sleep);
  if (op == "screen/get") return command(CommandType::SendScreen);

  return RouteOutcome::NoMatch;
}

// True for the .../result topics the device publishes itself, so a wildcard subscription does not
// feed our own replies back in as commands.
bool isResultEcho(const std::string& suffix) {
  static const std::string kSfx = "/result";
  if (suffix.size() <= kSfx.size() ||
      suffix.compare(suffix.size() - kSfx.size(), kSfx.size(), kSfx) != 0)
    return false;
  Command probe;
  std::string payload;
  std::string ignored;
  return routeMqtt(suffix.substr(0, suffix.size() - kSfx.size()), payload, probe, ignored) !=
         RouteOutcome::NoMatch;
}

std::string errorJson(const char* code, const std::string& message, const std::string& field) {
  std::string out = "{\"error\":{\"code\":\"";
  out += code;
  out += "\",\"message\":";
  appendJsonString(out, message);
  if (!field.empty()) {
    out += ",\"field\":";
    appendJsonString(out, field);
  }
  out += "}}";
  return out;
}

HttpResult errorResult(int status, const char* code, const std::string& message,
                       const std::string& field) {
  return {status, "application/json", errorJson(code, message, field)};
}

HttpResult errorResult(int status, const char* code, const char* message, const char* field) {
  return errorResult(status, code, std::string(message), std::string(field));
}

HttpResult unauthorized(const char* message) { return errorResult(401, "unauthorized", message); }

bool sameOrigin(std::string_view origin, std::string_view host,
                std::size_t originCount, std::size_t hostCount, bool required) {
  if (originCount == 0) return !required;
  if (originCount != 1 || hostCount != 1 || host.empty()) return false;
  if (origin.substr(0, 7) == "http://") return origin.substr(7) == host;
  if (origin.substr(0, 8) == "https://") return origin.substr(8) == host;
  return false;
}

namespace {

struct ErrorShape {
  const char* code;
  int status;
  const char* message;
};

ErrorShape shapeFor(DispatchResult r) {
  switch (r) {
    case DispatchResult::ParseError:
      return {"invalidJson", 400, nullptr};
    case DispatchResult::ValidationError:
      return {"validationFailed", 422, "invalid value"};
    case DispatchResult::NotFound:
      return {"notFound", 404, nullptr};
    case DispatchResult::Conflict:
      return {"scriptChanged", 409, "script changed"};
    case DispatchResult::Capacity:
      return {"insufficientStorage", 507, "storage full"};
    case DispatchResult::Unavailable:
      return {"unavailable", 503, "not available"};
    case DispatchResult::Busy:
      return {"serviceBusy", 503, "busy, try again"};
    case DispatchResult::Failed:
    case DispatchResult::Unknown:
    default:
      return {"internalError", 500, "command failed"};
  }
}

std::string messageFor(const DispatchDetail& detail, const char* fallback) {
  return detail.message.empty() ? fallback : detail.message;
}

}

HttpResult httpResponse(const Command& cmd, DispatchResult r, const DispatchDetail& detail) {
  HttpResult res;
  switch (r) {
    case DispatchResult::Ok:
      res.status = 200;
      // Script writes answer 200 with the compile error inside the body: storing the script
      // succeeded, only running it did not.
      if (cmd.type == CommandType::ScriptSet || cmd.type == CommandType::ScriptConfigSet ||
          cmd.type == CommandType::BuiltinAppConfigSet ||
          cmd.type == CommandType::ScriptDataSet) {
        res.body = "{\"ok\":true,\"name\":";
        appendJsonString(res.body, cmd.name);
        if (detail.message.empty()) {
          res.body += ",\"error\":null}";
        } else {
          res.body += ",\"error\":{\"message\":";
          appendJsonString(res.body, detail.message);
          if (detail.line > 0) res.body += ",\"line\":" + std::to_string(detail.line);
          if (!detail.hook.empty()) {
            res.body += ",\"hook\":";
            appendJsonString(res.body, detail.hook);
          }
          res.body += "}}";
        }
      } else {
        res.body = "{\"ok\":true}";
      }
      return res;
    default:
      break;
  }

  const ErrorShape shape = shapeFor(r);
  res.status = shape.status;
  if (r == DispatchResult::Busy) res.retryAfterSeconds = 2;

  const char* fallback = shape.message;
  if (r == DispatchResult::ParseError) {
    fallback = "invalid JSON";
  } else if (r == DispatchResult::NotFound) {
    // Only the app case is guessed; the audio router writes its own message.
    fallback = cmd.type == CommandType::SwitchApp ? "app not found" : "not found";
  }
  res.body = errorJson(shape.code, messageFor(detail, fallback), detail.field);
  return res;
}

std::string mqttResult(DispatchResult r, const DispatchDetail& detail) {
  if (r == DispatchResult::Ok) return "{\"ok\":true}";
  const ErrorShape shape = shapeFor(r);
  const char* fallback = shape.message;
  if (r == DispatchResult::ParseError) fallback = "invalid JSON";
  else if (r == DispatchResult::NotFound) fallback = "not found";
  return mqttError(shape.code, messageFor(detail, fallback), detail.field);
}

std::string errorEvent(const char* source, const std::string& request, const std::string& body) {
  static constexpr char kMqtt[] = "{\"ok\":false,";
  static constexpr char kError[] = "\"error\":";
  if (body.empty()) return {};
  const std::size_t at = body.compare(0, sizeof(kMqtt) - 1, kMqtt) == 0 ? sizeof(kMqtt) - 1 : 1;
  if (body.compare(at, sizeof(kError) - 1, kError) != 0) return {};
  std::string out = "{\"source\":\"";
  out += source;
  out += "\",\"request\":";
  appendJsonString(out, request);
  out += ',';
  out.append(body, at, std::string::npos);
  return out;
}

}
}
