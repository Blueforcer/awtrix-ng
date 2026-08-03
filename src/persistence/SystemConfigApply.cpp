#include "persistence/SystemConfigApply.h"

#include "core/ConfigRules.h"

namespace awtrix {
namespace sysconfig {

bool apply(DeviceConfig& cfg, api::JsonReader obj, int& applied, ApplyError& err, Origin origin) {
  applied = 0;
  cfgrules::ConfigError cerr;
  // A restore is allowed to write empty strings to clear a field; an interactive edit is not,
  // because a blank box in the UI means "leave it alone".
  const bool allowEmptyClears = origin == Origin::Restore;
  if (!cfgrules::validateSystemRead(obj, cerr, allowEmptyClears)) {
    err = {422, "validationFailed", cerr.message, cerr.field};
    return false;
  }
  // Everything is merged into a copy and only assigned back once all the cross-field rules pass,
  // so a rejected request cannot leave the live config half applied.
  DeviceConfig merged = cfg;
  const std::string ssid = merged.wifiSsid, pass = merged.wifiPass;
  applied = merged.applyRead(obj);
  // A restored backup keeps the credentials this device is connected with — the backup may come
  // from another network, and taking its Wi-Fi settings would strand the device.
  if (origin == Origin::Restore) {
    merged.wifiSsid = ssid;
    merged.wifiPass = pass;
  }
  const cfgrules::IpSplit split = cfgrules::systemIpSplit(obj);
  if (split.present) {
    merged.ip = split.ip;
    merged.subnet = split.subnet;
    ++applied;
  }
  if (!cfgrules::validateMatrixGeometry(merged.panelWidth, merged.panels, cerr) ||
      !cfgrules::validateBrightnessWindow(merged.minBrightness, merged.maxBrightness, cerr) ||
      !cfgrules::validateStaticNet(merged.netStatic, merged.ip, merged.subnet, cerr) ||
      !cfgrules::validateMqttGate(merged.mqttEnabled, merged.mqttHost, cerr) ||
      !cfgrules::validateAuthGate(merged.authEnabled, merged.authUser, merged.authPass, cerr) ||
      !cfgrules::validateAudioPins(merged.pinI2sBclk, merged.pinI2sLrclk, merged.pinI2sDout,
                                   cerr)) {
    err = {422, "validationFailed", cerr.message, cerr.field};
    return false;
  }
  std::string pinErr;
  if (!merged.validatePins(pinErr)) {
    err = {400, "invalidPinConfig", pinErr, ""};
    return false;
  }
  cfg = merged;
  return true;
}

}
}
