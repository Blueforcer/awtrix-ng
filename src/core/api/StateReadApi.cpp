#include "core/api/StateReadApi.h"

#include "AppConfig.h"
#include "core/CoreEngine.h"
#include "core/api/ApiRouter.h"
#include "core/api/ScriptSoundsApi.h"
#include "core/api/StateJson.h"
#include "core/apps/BuiltinAppConfig.h"
#include "core/script/ScriptData.h"
#include "core/script/ScriptHost.h"

namespace awtrix::api {

StateReadResult readState(const std::string& method, const std::string& path,
                          const StateReadContext& context, std::string& body) {
  if (method != "GET") return {};
  if (path == "/api/v1/device") {
    body = context.deviceState ? context.deviceState(context.scripts != nullptr) : "{}";
    return {true};
  }
  if (path == "/api/v1/settings") {
    body = buildSettingsJson(context.engine);
    return {true};
  }
  if (path == "/api/v1/display") {
    body = buildDisplayJson(context.engine);
    return {true};
  }
  if (path == "/api/v1/display/screen") {
    body = buildScreenJson(context.screen);
    return {true};
  }
  if (path == "/api/v1/capabilities") {
    body = context.capabilities;
    return {true};
  }
  if (path == "/api/v1/version") {
    body = std::string("{\"version\":\"") + AWTRIX_NG_VERSION + "\"}";
    return {true};
  }
  if (path == "/version") {
    body = AWTRIX_NG_VERSION;
    return {true, 200, "text/plain"};
  }
  if (path == "/api/v1/audio") {
    body.clear();
    appendAudioJson(body, context.engine);
    return {true, 200, "application/json", true};
  }
  if (path == "/api/v1/audio/stations") {
    body = context.engine.stationsJson();
    return {true};
  }
  if (path == "/api/v1/apps") {
    body.clear();
    // A stopped interpreter must not hide scripts that are still present in storage.
    if (context.scripts || !context.storedScripts) {
      appendAppsJson(body, context.engine, context.scripts, nullptr, context.device);
    } else {
      const auto stored = context.storedScripts();
      appendAppsJson(body, context.engine, nullptr, &stored, context.device);
    }
    return {true, 200, "application/json", true};
  }
  if (path == "/api/v1/scripts/shared") {
    if (!context.scripts) {
      body = errorJson("unavailable", "no scripting");
      return {true, 503};
    }
    body.clear();
    appendSharedStateJson(body, context.scripts->sharedSnapshot());
    return {true, 200, "application/json", true};
  }
  if (path.rfind("/api/v1/apps/script/", 0) == 0 && !scriptsounds::match(path).matched) {
    const std::string name = path.substr(sizeof("/api/v1/apps/script/") - 1);
    if (!context.scriptSource) {
      body = errorJson("unavailable", "no scripting");
      return {true, 503};
    }
    if (!isValidAppName(name)) {
      body = errorJson("invalidName", "invalid name", "name");
      return {true, 400};
    }
    body.clear();
    if (!context.scriptSource(name, body)) {
      body = errorJson("notFound", "no such script");
      return {true, 404};
    }
    return {true, 200, "text/plain"};
  }
  bool builtin = false;
  const std::string configName = appConfigName(path, builtin);
  if (builtin) {
    if (!isValidAppName(configName)) {
      body = errorJson("invalidName", "invalid name", "name");
      return {true, 400};
    }
    if (!context.engine.hasBuiltin(configName)) {
      body = errorJson("notFound", "no such app");
      return {true, 404};
    }
    body.clear();
    builtinconfig::appendConfigJson(body, configName, context.engine.state().settings(),
                                   context.engine.clockFaces());
    return {true, 200, "application/json", true};
  }
  if (!configName.empty()) {
    const int status =
        script::configResponse(configName, context.scriptSource, context.scriptStore, body);
    return {true, status, "application/json", true};
  }
  const std::string dataName = appSubresourceName(path, "/data");
  if (!dataName.empty()) {
    const int status =
        script::dataResponse(dataName, context.scriptSource, context.scriptStore, body);
    return {true, status, "application/json", true};
  }
  return {};
}

}
