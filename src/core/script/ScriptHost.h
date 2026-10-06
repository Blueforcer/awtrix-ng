#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/apps/IApp.h"
#include "core/input/Buttons.h"
#include "core/script/AsyncQueue.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptApp.h"
#include "core/script/ScriptExtension.h"
#include "core/script/ScriptMeta.h"
#include "core/script/ScriptServices.h"
#include "core/script/SharedState.h"
#include "core/script/ScriptTimers.h"

namespace awtrix {
class AppRegistry;
}

namespace awtrix::script {

class ScriptHost {
 public:
  using Native = int (*)(bvm*);
  using AppHook = std::function<void(const std::string& id)>;

  ScriptHost(AppRegistry& registry, ScriptServices& services, AppHook onInstalled,
             AppHook onRemoved);
  ~ScriptHost();
  ScriptHost(const ScriptHost&) = delete;
  ScriptHost& operator=(const ScriptHost&) = delete;

  const ScriptServices& services() const { return svc_; }
  bool ready() const { return vmError_.empty(); }
  void setExtensionLifecycle(ScriptExtensionLifecycle* extensions) {
    svc_.extensionLifecycle = effective_.extensionLifecycle = extensions;
  }

  void setRunningScripts(std::vector<std::string> running);
  bool active(const std::string& name) const;
  bool isHeadless(const std::string& name) const;

  bool set(const std::string& name, const std::string& source,
           const std::string& storeJson = "");
  const std::string& lastRefusal() const { return lastRefusal_; }
  bool refusalIsTransient() const { return refusalTransient_; }
  bool refusalIsInvalid() const { return refusalInvalid_; }
  void remove(const std::string& name);
  bool isModule(const std::string& name) const { return modules_.count(name) != 0; }

  // An @ondemand script is installed without an instance and only built by launch(), from its
  // stored source and store; unload() drops the instance again and keeps it installed.
  bool isOnDemand(const std::string& name) const {
    const ScriptMeta* meta = metaOf(name);
    return meta && meta->onDemand;
  }
  bool isLoaded(const std::string& name) const { return apps_.count(name) != 0; }
  bool launch(const std::string& name);
  void unload(const std::string& name);

  void tick(const RenderCtx& ctx, const std::string& currentAppId,
            const std::string& incomingAppId = std::string());
  void staggerFirstLoops(int64_t stepMs);
  bool handleButton(const std::string& currentAppId, const std::string& btn);
  // held: the button is still down. Not pressed but held means the clock took the press, for its
  // menu: it ends for the app there, without a release.
  bool handleButtonState(const std::string& currentAppId, int button, bool pressed, bool held = false);
  bool wantsShow(const std::string& name);
  long durationMs(const std::string& name) const;
  bool scrollHolds(const std::string& name) const;

  void pushHttpResult(HttpResult r) { httpQueue_.push(std::move(r)); }
  void pushMqttMessage(MqttMessage m) { mqttQueue_.push(std::move(m)); }

  void defineNative(const char* name, Native fn, void* self);
  bool defineModule(const std::string& name, const std::string& source);
  bool deliver(const std::string& app, const char* what, const char* function, const std::string& a, const std::string& b,
               const std::string& c, const RenderCtx* ctx);
  bool deliverHook(const std::string& app, const char* hook, const std::string& event, const RenderCtx* ctx);
  void call(const char* function, const std::string& a, const std::string& b);

  std::size_t count() const {
    std::size_t n = apps_.size() + modules_.size();
    for (const auto& kv : meta_)
      if (kv.second.parsed.onDemand && !apps_.count(kv.first)) ++n;
    return n;
  }
  bool has(const std::string& name) const {
    return apps_.count(name) != 0 || modules_.count(name) != 0 || isOnDemand(name);
  }
  // The @ headers an installed script or module was read with; null for a name it does not know.
  const ScriptMeta* metaOf(const std::string& name) const {
    const auto it = meta_.find(name);
    return it == meta_.end() ? nullptr : &it->second.parsed;
  }
  ScriptError errorOf(const std::string& name) const;

  struct Info {
    ScriptError error;
    bool skipping = false;
    bool headless = false;
    bool module = false;
    bool config = false;
    bool onDemand = false;
    bool loaded = false;
    std::string importName;
    std::string metaName;
    std::string desc;
    std::string author;
    std::string version;
    std::string icons;
    std::string requirements;
    std::string needs;
    int displayWidth = 0;
    int displayHeight = 0;
  };
  std::map<std::string, Info> list() const;

  std::vector<SharedEntry> sharedSnapshot() const;

 private:
  class HttpAdapter : public IScriptHttp {
   public:
    explicit HttpAdapter(ScriptHost* h) : host_(h) {}
    bool request(const HttpRequest& req) override;

   private:
    ScriptHost* host_;
  };

  class MqttAdapter : public IScriptMqtt {
   public:
    explicit MqttAdapter(ScriptHost* h) : host_(h) {}
    void publish(const std::string& topic, const std::string& payload) override;
    void subscribe(const std::string& topic) override;
    void unsubscribeAll(const std::string& topic) override { (void)topic; }

   private:
    ScriptHost* host_;
  };

  // Which script a still-flying request belongs to, and when to give up on it.
  // dueMs is a monotonicMs() deadline, not a duration.
  struct HttpOwner {
    std::string script;
    int64_t dueMs = 0;
  };

  struct Module {
    std::string importName;
    ScriptError error;
  };

  struct InstalledMeta {
    ScriptMeta parsed;
    std::unique_ptr<ScriptError> dormantError;
  };

  void activate();
  void clearRefusal();
  bool admit(const std::string& name, std::size_t sourceBytes, bool growsVm);
  bool retire(const std::string& name);
  static const std::string& seededStore(const ScriptMeta& meta, const std::string& source,
                                        const std::string& storeJson, std::string& seeded);
  void instantiate(const std::string& name, const ScriptMeta& meta, const std::string& source,
                   const std::string& storeJson);
  void installDormant(const std::string& name, const ScriptMeta& meta, const std::string& source,
                      bool wasApp, std::unique_ptr<ScriptError> error);
  bool installModule(const std::string& name, const ScriptMeta& meta,
                     const std::string& source, const std::string& storeJson);
  bool refuseModule(const std::string& name, const ScriptMeta& meta);
  void reloadDependents(const std::string& importName, const std::string& skip);
  void reportHeap(const std::string& name, std::size_t vmBefore, std::size_t freeBefore) const;
  void purge(const std::string& name);
  void reportFrameTimes();
  void drainStoreFlush();
  void restartTurn() {
    if (svc_.application) svc_.application->restartTurn();
  }
  void drainHttp(const RenderCtx* ctx);
  void sweepHttp(const RenderCtx* ctx);
  void drainTimers(const RenderCtx* ctx);
  void drainMqtt(const RenderCtx* ctx);
  void updateVisibility(const std::string& currentAppId, const std::string& incomingAppId,
                        const RenderCtx* ctx);
  void forgetScript(const std::string& name);
  // Clock and sensor readings from the last tick, for VM entries that happen outside a
  // frame (http and mqtt callbacks, loop, install). Null until the first tick().
  const RenderCtx* lastCtx() const { return haveCtx_ ? &lastCtx_ : nullptr; }

  AppRegistry& registry_;
  ScriptServices& svc_;
  ScriptServices effective_;
  HttpAdapter httpAdapter_;
  MqttAdapter mqttAdapter_;
  AppHook onInstalled_, onRemoved_;

  BerryVM vm_;
  SharedState shared_;
  ScriptTimers timers_;
  std::string installingApp_;
  struct HeldButton {
    bool down = false;
    bool longSent = false;
    int64_t pressedAt = 0;
    int64_t repeatAt = 0;
    std::string owner;
  };
  HeldButton buttons_[input::kButtonCount];
  std::string vmError_;

  std::vector<std::string> running_;
  bool runningKnown_ = false;
  std::map<std::string, std::unique_ptr<ScriptApp>> apps_;
  std::map<std::string, Module> modules_;
  // Script name -> the module import names its source mentions. Drives reloadDependents().
  std::map<std::string, std::vector<std::string>> imports_;
  std::map<std::string, InstalledMeta> meta_;
  std::map<uint32_t, HttpOwner> httpOwner_;
  std::map<std::string, std::vector<std::string>> mqttSubs_;
  AsyncQueue<HttpResult, 16> httpQueue_;
  AsyncQueue<MqttMessage, 32> mqttQueue_;
  bool heapWarned_ = false;
  // Reinstalling a module reinstalls its importers, which may themselves be modules. The
  // depth cap is what stops a circular import from reinstalling forever.
  static constexpr int kMaxReloadDepth = 3;
  int reloadDepth_ = 0;
  std::string lastRefusal_;
  bool refusalTransient_ = false;
  bool refusalInvalid_ = false;
  int64_t lastLoopMs_ = 0;
  RenderCtx lastCtx_;
  bool haveCtx_ = false;
};

}
