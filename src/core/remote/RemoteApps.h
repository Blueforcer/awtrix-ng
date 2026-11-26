#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/Services.h"
#include "core/apps/AppRegistry.h"
#include "core/apps/IApp.h"
#include "core/remote/RemoteFrames.h"
#include "core/remote/RemoteWire.h"

namespace awtrix {
namespace remote {

// One app of the script host as the clock sees it: it draws the last picture the host sent for it,
// or black before the first one.
class RemoteApp : public IApp {
 public:
  RemoteApp(std::string id, const RemoteFrames& frames) : id_(std::move(id)), frames_(frames) {}
  const std::string& id() const override { return id_; }
  void render(Canvas& canvas, const RenderCtx& ctx) override;

 private:
  std::string id_;
  const RemoteFrames& frames_;
};

// The script service of a clock whose scripts run on a script host. The host's app list decides
// which apps exist; each one sits in the app registry and the rotation like a local script app.
// Every change to a script goes on to the host while the link is up and is refused while it is
// down. While the link is down none of the apps wants to be shown, takes buttons or holds the
// rotation, so the rotation skips them.
class RemoteApps : public IScriptService {
 public:
  using Notify = std::function<void(const std::string& name)>;
  using Hold = std::function<void(bool hold)>;
  using Forward = std::function<bool(const char* method, const std::string& path, std::string body)>;

  RemoteApps(AppRegistry& registry, const RemoteFrames& frames, Notify added, Notify removed, Hold hold);
  ~RemoteApps() override;
  RemoteApps(const RemoteApps&) = delete;
  RemoteApps& operator=(const RemoteApps&) = delete;

  void setForward(Forward forward) { forward_ = std::move(forward); }

  void apply(const wire::Apps& apps);
  void setLinkUp(bool up);
  void hold(bool on);
  bool owns(const std::string& name) const { return find(name) != nullptr; }
  bool takesButtons(const std::string& name) const;
  uint16_t revision() const { return revision_; }
  uint32_t offMask() const { return offMask_; }

  DispatchResult setScript(const std::string& name, const std::string& source,
                           DispatchDetail& detail) override;
  DispatchResult removeScript(const std::string& name, DispatchDetail& detail) override;
  DispatchResult updateScript(const std::string& name, const std::string& json,
                              DispatchDetail& detail) override;
  DispatchResult setScriptConfig(const std::string& name, const std::string& json,
                                 DispatchDetail& detail) override;
  DispatchResult setScriptData(const std::string& name, const std::string& json,
                               DispatchDetail& detail) override;
  bool scriptWantsShow(const std::string& name) override;
  long scriptDurationMs(const std::string& name) override;
  bool scriptScrollHolds(const std::string& name) override;
  bool scriptIsHeadless(const std::string& name) override;
  bool scriptIsOnDemand(const std::string& name) override;
  DispatchResult launchScript(const std::string& name, DispatchDetail& detail) override;
  void setRunningScripts(const std::vector<std::string>& running) override;

 private:
  struct Entry {
    RemoteApp app;
    uint8_t flags = 0;
    uint32_t durationMs = 0;
    uint8_t slot = 0;
    bool off = false;
  };

  const Entry* find(std::string_view name) const;
  Entry* find(std::string_view name);
  bool has(std::string_view name, uint8_t flag) const;
  DispatchResult forward(const char* method, const std::string& path, const std::string& body,
                         DispatchDetail& detail);
  void maskOff();

  AppRegistry& registry_;
  const RemoteFrames& frames_;
  Notify added_;
  Notify removed_;
  Hold hold_;
  std::vector<std::unique_ptr<Entry>> entries_;
  bool linkUp_ = false;
  bool held_ = false;
  bool applied_ = false;
  uint16_t revision_ = 0;
  uint32_t offMask_ = 0;
  Forward forward_;
};

}
}
