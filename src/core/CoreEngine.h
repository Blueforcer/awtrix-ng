#pragma once

#include <cstdint>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "AppConfig.h"
#include "core/Command.h"
#include "core/CommandBus.h"
#include "core/Dispatcher.h"
#include "core/Services.h"
#include "core/StateStore.h"
#include "core/apps/AppHost.h"
#include "core/notify/NotificationManager.h"
#include "core/payload/AppSpec.h"
#include "core/radio/StationList.h"

namespace awtrix {

// Owns the app rotation, the pushed and script apps, notifications and the command queue.
// Everything that changes what the panel shows goes through here.
class CoreEngine : public IAppService, public INotifyService, public IRadioStations {
 public:
  CoreEngine(ISoundService& sound, IDisplayService& display, ISystemService& system);

  // Runs the command inline and returns its result; HTTP and MQTT use this so they can answer the
  // caller. submit() only queues, and the next tick() drains it.
  DispatchResult execute(const Command& c);
  bool submit(const Command& c) { return bus_.push(c); }
  void tick(int64_t nowMs);
  const DispatchDetail& lastDetail() const { return lastDetail_; }

  void setOrderPersist(std::function<void(const std::string& json)> cb) {
    orderSaveFn_ = std::move(cb);
  }

  void setRadioService(IRadioService* radio) { radio_ = radio; }
  bool radioAvailable() const { return radio_ != nullptr; }
  uint32_t radioUnderruns() const { return radio_ ? radio_->underruns() : 0; }
  uint32_t radioDecodeUs() const { return radio_ ? radio_->decodeUs() : 0; }
  uint32_t radioStarvedMs() const { return radio_ ? radio_->starvedMs() : 0; }
  uint32_t radioBufferBytes() const { return radio_ ? radio_->bufferBytes() : 0; }

  void setStationPersist(std::function<void(const std::string& json)> cb) {
    stationSaveFn_ = std::move(cb);
  }
  const std::vector<radio::Station>& stations() const { return stations_; }

  void setScriptService(IScriptService* scripts) {
    scripts_ = scripts;
    rebuildAppList();
  }

  void syncScriptApp(const std::string& name);
  void removeScriptApp(const std::string& name);

  void setOverlayRegistry(const EffectRegistry* overlays) { overlays_ = overlays; }
  void setEffectRegistry(const EffectRegistry* effects) { effects_ = effects; }

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

  // Reported by the render pipeline once a notification has scrolled its requested passes. The
  // generation is matched in tick() so a late report cannot cut short the notification after it.
  void setNotificationPassesDone(uint32_t generation, bool done) {
    notifPassesDone_ = done;
    notifPassesDoneGen_ = generation;
  }
  void setRotationPassesDone(const std::string& appId, bool done) {
    rotationPassesDonePage_ = done ? appId : std::string();
  }
  void setScriptRotationPaused(bool b) { scriptRotationPaused_ = b; }
  bool scriptRotationPaused() const { return scriptRotationPaused_; }
  void scriptNextApp() { appHost_.next(now_); }
  void scriptPreviousApp() { appHost_.previous(now_); }
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
    return std::find(scriptApps_.begin(), scriptApps_.end(), name) != scriptApps_.end();
  }

  DispatchResult setPushedApp(const std::string& name, const std::string& json,
                              DispatchDetail& detail) override;
  void deletePushedApp(const std::string& name) override;
  bool setAppOrder(const std::string& json) override;
  bool switchApp(const std::string& nameOrJson) override;
  void nextApp() override;
  void previousApp() override;

  DispatchResult notify(const std::string& json, uint8_t source, DispatchDetail& detail) override;
  void dismiss() override;
  bool dismissNamed(const std::string& name) override;

  DispatchResult setStations(const std::string& json, DispatchDetail& detail) override;
  std::string stationsJson() const override { return radio::stationsToJson(stations_); }
  std::string stationUrl(const std::string& name) const override;
  std::string stationNameAt(int index) const override;

 private:
  void rebuildAppList();
  bool forgetArrangement(const std::string& name);
  bool validateSpecNames(const AppSpec& spec, DispatchDetail& detail) const;

  std::vector<radio::Station> stations_;
  std::function<void(const std::string&)> stationSaveFn_;
  IRadioService* radio_ = nullptr;

  struct PushedAppEntry {
    std::string name;
    AppSpec spec;
    int64_t receivedAtMs = 0;
    std::string arrayBase;
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
  std::vector<std::string> scriptApps_;
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
  bool scriptRotationPaused_ = false;
  ISoundService& sound_;
  IDisplayService& display_;
  ISystemService& system_;
  // Timestamp of the last tick, ms since boot. Commands executed between ticks use it as clock.
  int64_t now_ = 0;
};

}
