#pragma once

#include <optional>
#include <utility>

#include "core/ApplicationRuntime.h"
#include "core/script/ScriptService.h"
#include "core/script/ScriptSourceService.h"

namespace awtrix::script {

// Construct once, after the platform has supplied its script services.
class ScriptSlots {
 public:
  void begin(ApplicationRuntime& app, ScriptServices& services, bool enabled,
             ScriptService::SaveFn save, ScriptService::RemoveFn remove) {
    CoreEngine& engine = app.engine();
    if (enabled) {
      host_.emplace(app.apps(), services,
          [&engine](const std::string& id) { engine.syncScriptApp(id); },
          [&engine](const std::string& id) { engine.removeScriptApp(id); });
      running_.emplace(*host_, std::move(save), std::move(remove));
      engine.setScriptService(&*running_);
    } else {
      sources_.emplace(std::move(save), std::move(remove));
      engine.setScriptService(&*sources_);
    }
  }

  ScriptHost* host() { return host_ ? &*host_ : nullptr; }

 private:
  std::optional<ScriptHost> host_;
  std::optional<ScriptService> running_;
  std::optional<ScriptSourceService> sources_;
};

}
