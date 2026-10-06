#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/script/ScriptConfig.h"
#include "core/script/ScriptMeta.h"

namespace awtrix {
class Canvas;
class CoreEngine;
struct DeviceCapabilities;
namespace script {
class ScriptHost;
}

namespace api {

using StoredScriptsFn = std::function<std::vector<script::StoredScript>()>;
using DeviceStateFn = std::function<std::string(bool scriptingRunning)>;

// Borrowed for a single call on the application loop. The transport supplies platform facts
// and storage access, while these read endpoints share their routing and response contracts.
struct StateReadContext {
  CoreEngine& engine;
  const Canvas& screen;
  const std::string& capabilities;
  const script::ScriptHost* scripts;
  const script::ConfigTextFn& scriptSource;
  const script::ConfigTextFn& scriptStore;
  const StoredScriptsFn& storedScripts;
  const DeviceStateFn& deviceState;
  // What this device offers scripts; lets /api/v1/apps say whether each script fits.
  const DeviceCapabilities* device = nullptr;
};

struct StateReadResult {
  bool matched = false;
  int status = 200;
  const char* contentType = "application/json";
  // True when the caller may retain the body buffer for the next response.
  bool reuseBuffer = false;
};

// The caller owns the body so embedded transports can reuse their response arena. An
// unmatched method/path leaves it untouched for the next handler. Authentication remains
// the transport's responsibility and must run before this service.
StateReadResult readState(const std::string& method, const std::string& path,
                          const StateReadContext& context, std::string& body);

}
}
