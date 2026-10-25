#include "persistence/FsRestoreSink.h"

#include "core/Services.h"
#include "core/Settings.h"
#include "core/StateStore.h"
#include "core/api/JsonReader.h"
#include "persistence/AppOrderStore.h"
#include "persistence/IconOriginsStore.h"
#include "persistence/NvsSettings.h"
#include "persistence/SystemConfigApply.h"

namespace awtrix {
namespace backup {

FsRestoreSink::FsRestoreSink(DeviceConfig& live, StateStore* state, IRadioStations* stations,
                             std::function<void()> onConfigChanged)
    : live_(live),
      working_(live),
      state_(state),
      stations_(stations),
      onConfigChanged_(std::move(onConfigChanged)) {}

bool FsRestoreSink::applyWifi(const std::string& ssid, const std::string& pass, std::string&) {
  working_.wifiSsid = ssid;
  working_.wifiPass = pass;
  configTouched_ = true;
  return true;
}

bool FsRestoreSink::applySystem(const std::string& json, std::string& err) {
  if (!api::isWellFormed(json)) {
    err = "invalid JSON";
    return false;
  }
  sysconfig::ApplyError ae;
  int applied = 0;
  if (!sysconfig::apply(working_, api::JsonReader(json), applied, ae,
                        sysconfig::Origin::Restore)) {
    err = ae.message;
    return false;
  }
  configTouched_ = true;
  return true;
}

// Unlike the device config, settings are written and published straight away — they carry no
// risk of locking the user out, and the UI should reflect them without waiting for commit().
bool FsRestoreSink::applySettings(const std::string& json, std::string& err) {
  if (!api::isWellFormed(json)) {
    err = "invalid JSON";
    return false;
  }
  SettingsError se;
  if (!Settings::validateRead(api::JsonReader(json), se, Settings::UnknownKeys::Skip)) {
    err = se.message;
    return false;
  }
  Settings s;
  s.applyStored(api::JsonReader(json));
  nvs::saveSettings(s);
  if (state_) {
    state_->settings() = s;
    state_->emit(StateEvent::SettingsChanged);
  }
  return true;
}

bool FsRestoreSink::applyAppLoop(const std::string& json, std::string&) {
  apporder::save(json);
  return true;
}

// Checked and taken over at once like PUT /api/v1/audio/stations, which also stores the list.
bool FsRestoreSink::applyRadioStations(const std::string& json, std::string& err) {
  if (!stations_) {
    err = "no station list on this device";
    return false;
  }
  DispatchDetail detail;
  if (stations_->setStations(json, detail) == DispatchResult::Ok) return true;
  err = detail.field.empty() ? detail.message : detail.field + ": " + detail.message;
  return false;
}

bool FsRestoreSink::applyIconOrigins(const std::string& json, std::string& err) {
  return iconorigins::restore(iconorigins::storage(), json, err);
}

void FsRestoreSink::commit() {
  if (!configTouched_) return;
  live_ = working_;
  live_.save();
  if (onConfigChanged_) onConfigChanged_();
}

}
}
