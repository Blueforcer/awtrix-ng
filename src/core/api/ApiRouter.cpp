#include "core/api/ApiRouter.h"

#include <string>
#include <string_view>

#include "core/api/JsonReader.h"
#include "core/api/JsonText.h"
#include "core/script/ScriptServices.h"
#include "core/sound/Rtttl.h"

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

HttpResult errorResult(int status, const char* code, const std::string& message,
                       const std::string& field = "") {
  HttpResult r;
  r.status = status;
  r.body = errorJson(code, message, field);
  return r;
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

// Exactly one of "name", "rtttl" or "builtin". RTTTL is parsed here so a bad melody fails with
// a useful message instead of reaching the player.
// Every source of sound the device has, one key each. A station or a url is a stream and goes to
// the radio dispatch; the rest are one-shots.
RouteOutcome routeAudioPlay(const std::string& body, Source src, Command& cmd,
                            HttpResult& immediate) {
  JsonReader atClip, atMelody, atRtttl, atBuiltin, atStation, atIndex, atUrl;
  if (!readMembers(body, {{"clip", &atClip},
                          {"melody", &atMelody},
                          {"rtttl", &atRtttl},
                          {"builtin", &atBuiltin},
                          {"station", &atStation},
                          {"index", &atIndex},
                          {"url", &atUrl}})) {
    immediate = errorResult(400, "invalidJson",
                            src == Source::Mqtt ? "payload is not valid JSON"
                                                : "request body is not valid JSON");
    return RouteOutcome::Respond;
  }
  const int keys = present(atClip) + present(atMelody) + present(atRtttl) + present(atBuiltin) +
                   present(atStation) + present(atIndex) + present(atUrl);
  static constexpr const char* kOneOf =
      "exactly one of \"clip\", \"melody\", \"rtttl\", \"builtin\", \"station\", \"index\" or \"url\"";
  if (keys > 1) {
    immediate = errorResult(422, "validationFailed", std::string(kOneOf) + " is allowed", "clip");
    return RouteOutcome::Respond;
  }
  if (keys == 0) {
    immediate = errorResult(422, "validationFailed", std::string(kOneOf) + " is required", "clip");
    return RouteOutcome::Respond;
  }

  // A stream keeps the payload as sent: the radio dispatch reads station, index or url itself.
  if (present(atStation) || present(atIndex) || present(atUrl)) {
    cmd = make(CommandType::RadioPlay, src);
    cmd.payload = body;
    return RouteOutcome::Routed;
  }

  std::string value;
  if (atClip.appendString(value)) {
    cmd = make(CommandType::PlayClip, src);
    cmd.payload = value;
    return RouteOutcome::Routed;
  }
  if (atMelody.appendString(value)) {
    cmd = make(CommandType::PlayMelody, src);
    cmd.payload = value;
    return RouteOutcome::Routed;
  }
  if (atRtttl.appendString(value)) {
    const rtttl::Parse p = rtttl::parse(value);
    if (!p.ok) {
      immediate = errorResult(422, "validationFailed", p.describe(), "rtttl");
      return RouteOutcome::Respond;
    }
    cmd = make(CommandType::PlayRtttl, src);
    cmd.payload = value;
    return RouteOutcome::Routed;
  }
  if (atBuiltin.appendString(value)) {
    cmd = make(CommandType::R2D2, src);
    cmd.payload = value;
    return RouteOutcome::Routed;
  }
  immediate = errorResult(422, "validationFailed", std::string(kOneOf) + " must be a string",
                          "clip");
  return RouteOutcome::Respond;
}

}

bool isValidAppName(const std::string& name) {
  if (name.empty() || name.size() > 32) return false;
  for (char c : name) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

std::string configAppName(const std::string& path) {
  constexpr std::string_view kSuffix = "/config";
  const std::string tail = tailAfter(path, "/api/v1/apps/");
  if (tail.size() <= kSuffix.size()) return {};
  if (tail.compare(tail.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) return {};
  return tail.substr(0, tail.size() - kSuffix.size());
}

// Script uploads carry Berry source, not JSON, so the server has to pass the body through raw.
bool isRawBodyWrite(const std::string& method, const std::string& path) {
  return method == "PUT" && !tailAfter(path, "/api/v1/apps/script/").empty();
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
    out.error = "X-HTTP-Method-Override is only accepted on POST";
    return out;
  }
  if (want != "PUT" && want != "PATCH" && want != "DELETE") {
    out.error = "X-HTTP-Method-Override must be PUT, PATCH or DELETE";
    return out;
  }
  if (isRawBodyWrite(want, path)) {
    out.error = "X-HTTP-Method-Override cannot be used to upload a script source";
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
    cmd.payload = body;
    return RouteOutcome::Routed;
  };
  auto methodNotAllowed = [&](const char* allowed) {
    immediate = errorResult(405, "methodNotAllowed",
                            std::string("allowed method(s): ") + allowed);
    return RouteOutcome::Respond;
  };
  auto requireBody = [&](const char* hint) {
    immediate = errorResult(422, "validationFailed",
                            std::string("a JSON body is required; ") + hint);
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
    immediate = errorResult(400, "invalidName", "name must match [A-Za-z0-9_-]{1,32}", "name");
    return RouteOutcome::Respond;
  };

  {
    const std::string name = tailAfter(path, "/api/v1/apps/pushed/");
    if (!name.empty()) {
      if (!put) return methodNotAllowed("PUT");
      if (!isValidAppName(name)) return badName();
      if (body.empty() || body == "{}")
        return requireBody("use DELETE /api/v1/apps/{name} to remove the app");
      cmd = make(CommandType::SetPushedApp, src);
      cmd.name = name;
      cmd.payload = body;
      return RouteOutcome::Routed;
    }
  }

  if (path == "/api/v1/scripts/shared") {
    if (get) return RouteOutcome::NoMatch;
    return methodNotAllowed("GET");
  }

  {
    const std::string name = tailAfter(path, "/api/v1/apps/script/");
    if (!name.empty()) {
      if (get) return RouteOutcome::NoMatch;
      if (!put) return methodNotAllowed("GET, PUT");
      if (!isValidAppName(name)) return badName();
      if (body.empty()) {
        immediate = errorResult(422, "validationFailed", "request body must be the script source",
                                "source");
        return RouteOutcome::Respond;
      }
      if (body.size() > script::maxSourceBytes()) {
        immediate = errorResult(413, "payloadTooLarge",
                                "script source exceeds " +
                                    std::to_string(script::maxSourceBytes()) + " bytes",
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
    const std::string tail = tailAfter(path, "/api/v1/apps/");
    const std::string suffix = "/config";
    if (tail.size() > suffix.size() &&
        tail.compare(tail.size() - suffix.size(), suffix.size(), suffix) == 0) {
      const std::string name = tail.substr(0, tail.size() - suffix.size());
      if (get) return RouteOutcome::NoMatch;
      if (!patch) return methodNotAllowed("GET, PATCH");
      if (!isValidAppName(name)) return badName();
      if (body.empty()) return requireBody("send the settings to change");
      cmd = make(CommandType::ScriptConfigSet, src);
      cmd.name = name;
      cmd.payload = std::move(body);
      return RouteOutcome::Routed;
    }
  }

  // Reached only after the pushed/, script/ and /config prefixes above, so any leftover slash
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
      if (body.empty() || body == "{}") return requireBody("use DELETE to turn the mood light off");
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
        immediate = errorResult(404, "notFound", "indicator id must be 1..3");
        return RouteOutcome::Respond;
      }
      if (put) {
        if (body.empty() || body == "{}") return requireBody("use DELETE to turn the indicator off");
        cmd = make(CommandType::SetIndicator, src);
        cmd.arg = id;
        cmd.payload = body;
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
      if (body.empty()) return requireBody("name a clip, a melody, a station or a url");
      return routeAudioPlay(body, src, cmd, immediate);
    }
    return methodNotAllowed("POST");
  }

  // Stops everything the output is doing, stream included: one output, one stop.
  if (path == "/api/v1/audio/stop") {
    if (post) return command(CommandType::StopAudio);
    return methodNotAllowed("POST");
  }

  if (path == "/api/v1/audio/stations") {
    if (put) {
      if (body.empty()) return requireBody("send {\"stations\":[...]}");
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
RouteOutcome routeMqtt(const std::string& suffix, const std::string& payload,
                       Command& cmd, std::string& resultPayload) {
  const Source src = Source::Mqtt;
  auto command = [&](CommandType t) {
    cmd = make(t, src);
    cmd.payload = payload;
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
        resultPayload = mqttError("invalidName", "name must match [A-Za-z0-9_-]{1,32}", "name");
        return RouteOutcome::Respond;
      }
      cmd = make(CommandType::SetPushedApp, src);
      cmd.name = name;
      cmd.payload = payload;
      cmd.clear = payload.empty();
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
    cmd.payload = payload;
    cmd.clear = payload.empty();
    return RouteOutcome::Routed;
  }
  {
    const std::string tail = tailAfter(op, "indicators/");
    if (!tail.empty()) {
      const int id = indicatorId(tail);
      if (id == 0) return RouteOutcome::NoMatch;
      cmd = make(CommandType::SetIndicator, src);
      cmd.arg = id;
      cmd.payload = payload;
      cmd.clear = payload.empty();
      return RouteOutcome::Routed;
    }
  }
  if (op == "audio/play") {
    HttpResult imm;
    const RouteOutcome o = routeAudioPlay(payload, src, cmd, imm);
    if (o == RouteOutcome::Respond) resultPayload = mqttError(imm.body);
    return o;
  }
  if (op == "audio/stop") return command(CommandType::StopAudio);
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
  std::string ignored;
  return routeMqtt(suffix.substr(0, suffix.size() - kSfx.size()), "", probe, ignored) !=
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
    case DispatchResult::Capacity:
      return {"insufficientStorage", 507, "storage capacity reached"};
    case DispatchResult::Unavailable:
      return {"unavailable", 503, "not available on this device"};
    case DispatchResult::Busy:
      return {"serviceBusy", 503, "device is busy, try again"};
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
      if (cmd.type == CommandType::ScriptSet || cmd.type == CommandType::ScriptConfigSet) {
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
    fallback = "request body is not valid JSON";
  } else if (r == DispatchResult::NotFound) {
    fallback = cmd.type == CommandType::SwitchApp     ? "app not found"
               : cmd.type == CommandType::PlayClip    ? "no clip of that name"
               : cmd.type == CommandType::PlayMelody  ? "no melody of that name"
               : cmd.type == CommandType::PlaySound   ? "no sound of that name"
                                                      : "not found";
  }
  res.body = errorJson(shape.code, messageFor(detail, fallback), detail.field);
  return res;
}

std::string mqttResult(DispatchResult r, const DispatchDetail& detail) {
  if (r == DispatchResult::Ok) return "{\"ok\":true}";
  const ErrorShape shape = shapeFor(r);
  const char* fallback = shape.message;
  if (r == DispatchResult::ParseError) fallback = "payload is not valid JSON";
  else if (r == DispatchResult::NotFound) fallback = "not found";
  return mqttError(shape.code, messageFor(detail, fallback), detail.field);
}

}
}
