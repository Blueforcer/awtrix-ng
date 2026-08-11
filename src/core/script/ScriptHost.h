#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/apps/IApp.h"
#include "core/script/AsyncQueue.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptApp.h"
#include "core/script/ScriptMeta.h"
#include "core/script/ScriptServices.h"
#include "core/script/SharedState.h"

namespace awtrix {
class AppRegistry;
}

namespace awtrix::script {

class ScriptHost {
 public:
  using AppHook = std::function<void(const std::string& id)>;

  ScriptHost(AppRegistry& registry, ScriptServices& services, AppHook onInstalled,
             AppHook onRemoved);
  ~ScriptHost();
  ScriptHost(const ScriptHost&) = delete;
  ScriptHost& operator=(const ScriptHost&) = delete;

  const ScriptServices& services() const { return svc_; }

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

  void tick(const RenderCtx& ctx, const std::string& currentAppId,
            const std::string& incomingAppId = std::string());
  void staggerFirstLoops(int64_t stepMs);
  bool handleButton(const std::string& currentAppId, const std::string& btn);
  bool wantsShow(const std::string& name);
  long durationMs(const std::string& name) const;
  bool scrollHolds(const std::string& name) const;

  void pushHttpResult(HttpResult r) { httpQueue_.push(std::move(r)); }
  void pushMqttMessage(MqttMessage m) { mqttQueue_.push(std::move(m)); }

  std::size_t count() const { return apps_.size() + modules_.size(); }
  bool has(const std::string& name) const {
    return apps_.count(name) != 0 || modules_.count(name) != 0;
  }
  ScriptError errorOf(const std::string& name) const;

  struct Info {
    ScriptError error;
    bool skipping = false;
    bool headless = false;
    bool module = false;
    bool config = false;
    std::string importName;
    std::string metaName;
    std::string desc;
    std::string author;
    std::string version;
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

  void activate();
  bool installModule(const std::string& name, const ScriptMeta& meta,
                     const std::string& source, const std::string& storeJson);
  bool refuseModule(const std::string& name, const ScriptMeta& meta);
  void reloadDependents(const std::string& importName, const std::string& skip);
  void reportHeap(const std::string& name, std::size_t vmBefore, std::size_t freeBefore) const;
  void purge(const std::string& name);
  void reportFrameTimes();
  void drainStoreFlush();
  void drainHttp(const RenderCtx* ctx);
  void sweepHttp(const RenderCtx* ctx);
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
  std::string vmError_;

  std::vector<std::string> running_;
  bool runningKnown_ = false;
  std::map<std::string, std::unique_ptr<ScriptApp>> apps_;
  std::map<std::string, Module> modules_;
  // Script name -> the module import names its source mentions. Drives reloadDependents().
  std::map<std::string, std::vector<std::string>> imports_;
  std::map<std::string, ScriptMeta> meta_;
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
