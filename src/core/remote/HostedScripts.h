#pragma once

#include "core/ApplicationRuntime.h"
#include "core/remote/HostActs.h"
#include "core/remote/RemoteApps.h"
#include "core/remote/RemoteButtons.h"
#include "core/remote/RemoteFrames.h"

namespace awtrix {
namespace remote {

struct HostedScripts {
  explicit HostedScripts(ApplicationRuntime& app)
      : apps(app.apps(), frames,
             [&engine = app.engine()](const std::string& id) { engine.syncScriptApp(id); },
             [&engine = app.engine()](const std::string& id) { engine.removeScriptApp(id); },
             [&engine = app.engine()](bool hold) { engine.setScriptRotationPaused(hold); }),
        acts(app.engine(), apps) {
    frames.begin(app.canvas().width(), app.canvas().height());
  }
  HostedScripts(const HostedScripts&) = delete;
  HostedScripts& operator=(const HostedScripts&) = delete;

  RemoteFrames frames;
  RemoteApps apps;
  RemoteButtons buttons;
  HostActs acts;
};

}
}
