#include "persistence/FsRestoreSink.h"

#include "core/Settings.h"
#include "core/StateStore.h"
#include "core/api/JsonReader.h"
#include "persistence/AppOrderStore.h"
#include "persistence/RadioStore.h"
#include "persistence/NvsSettings.h"
#include "persistence/SystemConfigApply.h"

namespace awtrix {
namespace backup {

FsRestoreSink::FsRestoreSink(DeviceConfig& live, StateStore* state,
                             std::function<void()> onConfigChanged)
    : live_(live), working_(live), state_(state), onConfigChanged_(std::move(onConfigChanged)) {}

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
  if (!Settings::validateRead(api::JsonReader(json), se)) {
    err = se.message;
    return false;
  }
  Settings s;
  s.applyRead(api::JsonReader(json));
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

bool FsRestoreSink::applyRadioStations(const std::string& json, std::string&) {
  radiostore::save(json);
  return true;
}

void FsRestoreSink::commit() {
  if (!configTouched_) return;
  live_ = working_;
  live_.save();
  if (onConfigChanged_) onConfigChanged_();
}

}
}
