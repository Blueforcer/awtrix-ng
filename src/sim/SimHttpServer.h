#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/script/ScriptMeta.h"

namespace awtrix::script {
class ScriptHost;
}

namespace awtrix {

class CoreEngine;
class Canvas;
class SimBoard;
struct DeviceConfig;

class SimHttpServer {
 public:
  SimHttpServer();
  ~SimHttpServer();

  bool begin(uint16_t port, CoreEngine& engine, SimBoard& board, Canvas& screen,
             const std::string& uid, DeviceConfig& cfg, const std::string& webuiFile);
  void setCapabilitiesJson(std::string j);
  void setOnConfigChanged(std::function<void()> cb);
  void setOnAssetsChanged(std::function<void()> cb);
  using ScriptSourceFn = std::function<bool(const std::string& name, std::string& out)>;
  using StoredScriptsFn = std::function<std::vector<script::StoredScript>()>;
  void setScripts(const script::ScriptHost* host, ScriptSourceFn readSource,
                  ScriptSourceFn readStore, StoredScriptsFn stored = nullptr);
  // Runs the request handlers the listener thread parked. Must be called from the main loop: it is
  // what lets routes touch the engine without any locking.
  void tick();
  void stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
