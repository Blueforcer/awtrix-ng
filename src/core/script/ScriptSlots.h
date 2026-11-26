#pragma once

#include <memory>
#include <optional>
#include <utility>

#include "core/ApplicationRuntime.h"
#include "core/remote/HostedScripts.h"
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
      host_->setSwitchedOff([&engine](const std::string& id) { return !engine.isEnabled(id); });
      running_.emplace(*host_, std::move(save), std::move(remove));
      engine.setScriptService(&*running_);
    } else {
      sources_.emplace(std::move(save), std::move(remove));
      engine.setScriptService(&*sources_);
    }
  }

  // The clock's scripts run on a script host: no script engine here, only the host's apps, which
  // draw the pictures the host sends.
  void beginRemote(ApplicationRuntime& app) {
    hosted_ = std::make_unique<remote::HostedScripts>(app);
    app.engine().setScriptService(&hosted_->apps);
  }

  ScriptHost* host() { return host_ ? &*host_ : nullptr; }
  remote::HostedScripts* hosted() { return hosted_.get(); }

 private:
  std::optional<ScriptHost> host_;
  std::optional<ScriptService> running_;
  std::optional<ScriptSourceService> sources_;
  std::unique_ptr<remote::HostedScripts> hosted_;
};

}
