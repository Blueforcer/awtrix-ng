#pragma once

#include <functional>
#include <string>

#include "core/backup/Restore.h"
#include "persistence/DeviceConfig.h"

namespace awtrix {
class StateStore;
namespace backup {

// Applies a backup to this device. Device config is staged in working_ and only reaches flash in
// commit(), so a backup that fails validation halfway leaves the running config untouched.
class FsRestoreSink : public RestoreSink {
 public:
  FsRestoreSink(DeviceConfig& live, StateStore* state, std::function<void()> onConfigChanged = {});

  bool applyWifi(const std::string& ssid, const std::string& pass, std::string& err) override;
  bool applySystem(const std::string& json, std::string& err) override;
  bool applySettings(const std::string& json, std::string& err) override;
  bool applyAppLoop(const std::string& json, std::string& err) override;
  bool applyRadioStations(const std::string& json, std::string& err) override;
  void commit() override;


 protected:
  DeviceConfig& live_;
  DeviceConfig working_;
  StateStore* state_;
  std::function<void()> onConfigChanged_;
  bool configTouched_ = false;
};

}
}
