#pragma once

#include <cstdint>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "AppConfig.h"
#include "core/Command.h"
#include "core/CommandBus.h"
#include "core/Dispatcher.h"
#include "core/Services.h"
#include "core/StateStore.h"
#include "core/apps/AppHost.h"
#include "core/apps/BuiltinNames.h"
#include "core/notify/NotificationManager.h"
#include "core/payload/AppSpec.h"
#include "core/render/PageContent.h"
#include "core/render/FontCatalog.h"
#include "core/radio/StationList.h"

namespace awtrix {

// Owns the app rotation, the pushed and script apps, notifications and the command queue.
// Everything that changes what the panel shows goes through here.
class CoreEngine : public BuiltinAppProfile, public IAppService, public INotifyService, public IRadioStations {
 public:
  CoreEngine(sound::AudioRouter& audio, IDisplayService& display, ISystemService& system,
             std::vector<std::string> builtins = defaultBuiltinNames());

  // HTTP and MQTT execute inline to return a result; submit() queues for the next tick.
  DispatchResult execute(Command& c);
  DispatchResult execute(Command&& c) { return execute(c); }
  bool submit(Command c) { return bus_.push(std::move(c)); }
  void tick(int64_t nowMs);
  const DispatchDetail& lastDetail() const { return lastDetail_; }

  void setOrderPersist(std::function<void(const std::string& json)> cb) {
    orderSaveFn_ = std::move(cb);
  }

  // Telemetry stays here rather than behind the router: the router deliberately hands out no
  // sink of its own, so nothing can reach past it to play something.
  void setPcmSink(sound::IPcmSink* pcm) { pcm_ = pcm; }
  bool radioAvailable() const { return pcm_ != nullptr; }
  const sound::AudioRouter& audio() const { return audio_; }
  uint32_t radioUnderruns() const { return pcm_ ? pcm_->underruns() : 0; }
  uint32_t radioDecodeUs() const { return pcm_ ? pcm_->decodeUs() : 0; }
  uint32_t radioStarvedMs() const { return pcm_ ? pcm_->starvedMs() : 0; }
  uint32_t radioBufferBytes() const { return pcm_ ? pcm_->bufferBytes() : 0; }

  void setStationPersist(std::function<void(const std::string& json)> cb) {
    stationSaveFn_ = std::move(cb);
  }
  const std::vector<radio::Station>& stations() const { return stations_; }

  IScriptService* scriptService() const { return scripts_; }
  // Attach before restoring scripts: installs are filed as rotation or on-demand apps by asking it.
  void setScriptService(IScriptService* scripts) {
    scripts_ = scripts;
    rebuildAppList();
  }

  // A built-in this device draws now: its sensor is there and no pushed app took its name.
  bool hasBuiltin(const std::string& name) const;
  void syncScriptApp(const std::string& name);
  void removeScriptApp(const std::string& name);

  void setOverlayRegistry(const EffectRegistry* overlays) { overlays_ = overlays; }
  void setEffectRegistry(const EffectRegistry* effects) { effects_ = effects; }
  void setFontCatalog(const FontCatalog* fonts) { fonts_ = fonts; }

  StateStore& state() { return state_; }
  AppHost& appHost() { return appHost_; }
  void setBatteryAvailable(bool b) { state_.runtime().hasBattery = b; rebuildAppList(); }
  void setHumidityAvailable(bool b) { state_.runtime().hasHumidity = b; rebuildAppList(); }
  void setTemperatureAvailable(bool b) { state_.runtime().hasTemperature = b; rebuildAppList(); }
  void setPressureAvailable(bool b) { state_.runtime().hasPressure = b; }
  void setLightSensorAvailable(bool b) { state_.runtime().hasLightSensor = b; }
  void setRotationHold(bool b) { rotationHold_ = b; }
  bool rotationHold() const { return rotationHold_; }

  void setNotificationHold(bool b) { notificationHold_ = b; }
  bool notificationHold() const { return notificationHold_; }
  // Called on the loop at an asset event, before the next rotation/notification tick.
  void invalidateContentAssets();

  // Reported by the render pipeline once a notification has scrolled its requested passes. The
  // generation is matched in tick() so a late report cannot cut short the notification after it.
  void setNotificationPassesDone(uint32_t generation, bool done) {
    notifPassesDone_ = done;
    notifPassesDoneGen_ = generation;
  }
  void setRotationPassesDone(const std::string& appId, bool done, uint64_t revision = 0) {
    rotationPassesDonePage_ = done ? appId : std::string();
    rotationPassesDoneRevision_ = revision;
  }
  bool endsOnScrollPasses(const AppSpec& spec, bool isNotification) const {
    bool repeats = spec.repeat > 0;
    if (spec.extras().content) repeats = repeats || spec.extras().content->repeats();
    if (!repeats || spec.durationMs > 0) return false;
    if (isNotification) return !spec.hold;
    return state_.settings().autoTransition && !scriptRotationPaused_ && appHost_.count() > 1;
  }
  void setScriptRotationPaused(bool b) { scriptRotationPaused_ = b; }
  bool scriptRotationPaused() const { return scriptRotationPaused_; }
  void scriptNextApp() { appHost_.next(now_); }
  void scriptPreviousApp() { appHost_.previous(now_); }
  void scriptRestartTurn() { appHost_.restartTurn(now_); }
  bool scriptShowApp(const std::string& name) { return appHost_.transitionTo(name, now_); }
  NotificationManager& notifications() { return notifs_; }
  std::vector<std::string> allApps() const;
  std::string appOrderJson() const;
  const std::vector<std::string>& appOrder() const { return order_; }
  std::vector<std::string> knownApps() const;
  bool isPresent(const std::string& name) const;
  int slotOf(const std::string& name) const;
  bool isInLoop(const std::string& name) const {
    const auto& ids = appHost_.ids();
    return std::find(ids.begin(), ids.end(), name) != ids.end();
  }
  bool isEnabled(const std::string& name) const {
    return std::find(disabled_.begin(), disabled_.end(), name) == disabled_.end();
  }
  const std::string& currentAppId() const { return appHost_.currentId(); }
  const std::string& incomingAppId() const { return appHost_.incomingId(); }
  bool hasNotification() const { return notifs_.hasCurrent(); }
  const AppSpec* pushedApp(const std::string& name) const;
  bool isScriptApp(const std::string& name) const {
    return std::find(scriptApps_.begin(), scriptApps_.end(), name) != scriptApps_.end() ||
           isOnDemandApp(name);
  }
  bool isOnDemandApp(const std::string& name) const {
    return std::find(onDemandApps_.begin(), onDemandApps_.end(), name) != onDemandApps_.end();
  }
  // @ondemand scripts in install order. They never join the rotation; one at a time runs as the
  // session, alone on the panel, until endSession() unloads it and the rotation carries on.
  const std::vector<std::string>& onDemandApps() const { return onDemandApps_; }
  const std::string& sessionApp() const { return session_; }
  bool inSession() const { return !session_.empty(); }
  DispatchResult startSession(const std::string& name, DispatchDetail& detail);
  void endSession();
  // The session's own script asks to end it. The unload waits for the next tick(), so the script
  // is never dropped while it is still running the call that asked.
  bool requestSessionEnd(const std::string& name);

  DispatchResult setPushedApp(const std::string& name, const std::string& json,
                              DispatchDetail& detail) override;
  void deletePushedApp(const std::string& name) override;
  bool setAppOrder(const std::string& json) override;
  void setAppEnabled(const std::string& name, bool enabled) override;
  DispatchResult switchApp(const std::string& nameOrJson, DispatchDetail& detail) override;
  void nextApp() override;
  void previousApp() override;
  DispatchResult setBuiltinAppConfig(const std::string& name, const std::string& json,
                                     DispatchDetail& detail) override;

  DispatchResult notify(const std::string& json, uint8_t source, DispatchDetail& detail) override;
  // A notification a running script raised: a sound it names is looked for in that script's own
  // folder first, then in /MP3.
  DispatchResult notifyFromScript(const std::string& script, const std::string& json,
                                  DispatchDetail& detail);
  void dismiss() override;
  bool dismissNamed(const std::string& name) override;

  DispatchResult setStations(const std::string& json, DispatchDetail& detail) override;
  std::string stationsJson() const override { return radio::stationsToJson(stations_); }
  std::string stationUrl(const std::string& name) const override;
  std::string stationNameAt(int index) const override;

 private:
  DispatchResult pushNotification(const std::string& json, const std::string& soundScript,
                                  DispatchDetail& detail);
  void rebuildAppList();
  void leaveSession();
  void forgetSession();
  void dropRetired(std::vector<std::string>& names) const;
  bool forgetArrangement(const std::string& name);
  bool validateSpecNames(const AppSpec& spec, DispatchDetail& detail) const;
  bool prepareSpecContent(AppSpec& spec, DispatchDetail& detail);
  void resetContentCompletion(bool assets);

  std::vector<radio::Station> stations_;
  std::function<void(const std::string&)> stationSaveFn_;
  sound::IPcmSink* pcm_ = nullptr;

  struct PushedAppEntry {
    std::string name;
    AppSpec spec;
    int64_t receivedAtMs = 0;
    std::string arrayBase;
    // Counts up once per newly created app and never on an update, so the loop can follow the
    // order the apps first arrived in while the vector stays sorted by name.
    uint32_t arrival = 0;
  };
  // pushedApps_ is kept sorted by name so lookups can binary-search it.
  std::vector<PushedAppEntry>::iterator pushedLowerBound(const std::string& name);
  std::vector<PushedAppEntry>::const_iterator pushedLowerBound(const std::string& name) const;

  StateStore state_;
  AppHost appHost_;
  NotificationManager notifs_;
  CommandBus bus_;
  Dispatcher dispatcher_;
  std::vector<PushedAppEntry> pushedApps_;
  uint32_t nextArrival_ = 0;
  std::vector<std::string> scriptApps_;
  std::vector<std::string> onDemandApps_;
  std::string session_;
  std::string sessionReturn_;
  bool sessionEndRequested_ = false;
  std::vector<std::string> builtinApps_;
  // The user's arrangement: order_ is the wanted sequence, disabled_ the apps kept out of the loop.
  // Both may name apps that do not exist at the moment, so a returning sender keeps its slot.
  std::vector<std::string> order_;
  std::vector<std::string> disabled_;
  std::function<void(const std::string&)> orderSaveFn_;
  IScriptService* scripts_ = nullptr;
  const EffectRegistry* overlays_ = nullptr;
  const EffectRegistry* effects_ = nullptr;
  DispatchDetail lastDetail_{};
  bool rotationHold_ = false;
  bool notificationHold_ = false;
  bool notifPassesDone_ = false;
  uint32_t notifPassesDoneGen_ = 0;
  std::string rotationPassesDonePage_;
  uint64_t rotationPassesDoneRevision_ = 0;
  const FontCatalog* fonts_ = nullptr;
  bool scriptRotationPaused_ = false;
  sound::AudioRouter& audio_;
  IDisplayService& display_;
  ISystemService& system_;
  // Timestamp of the last tick, ms since boot. Commands executed between ticks use it as clock.
  int64_t now_ = 0;
};

}
