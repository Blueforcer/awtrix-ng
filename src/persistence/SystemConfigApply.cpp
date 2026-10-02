#include "persistence/SystemConfigApply.h"

#include "core/ConfigRules.h"

namespace awtrix {
namespace sysconfig {
namespace {

bool sameWiring(const DeviceConfig& a, const DeviceConfig& b) {
  return a.panelStart == b.panelStart && a.panelWiring == b.panelWiring &&
         a.panelColorOrder == b.panelColorOrder && a.panelSerpentine == b.panelSerpentine &&
         a.panelChainReverse == b.panelChainReverse &&
         a.panelChainSerpentine == b.panelChainSerpentine;
}

// On a board with fixed wiring the pins and the panel are part of the board, so a restore keeps
// them, as it keeps the Wi-Fi: a backup from a board wired differently still restores the rest.
void keepFixedWiring(DeviceConfig& merged, const DeviceConfig& live, const pins::SocProfile& soc) {
  const int height = merged.panelHeight;
  adoptFixedBoard(merged, soc);
  if (!soc.fixedPanelWidth) return;
  if (merged.panelHeight != height) merged.panelHeight = live.panelHeight;
  merged.panelStart = live.panelStart;
  merged.panelWiring = live.panelWiring;
  merged.panelColorOrder = live.panelColorOrder;
  merged.panelSerpentine = live.panelSerpentine;
  merged.panelChainReverse = live.panelChainReverse;
  merged.panelChainSerpentine = live.panelChainSerpentine;
}

}

void adoptFixedBoard(DeviceConfig& cfg, const pins::SocProfile& soc) {
  if (soc.fixedWiring) cfg.setPinSet(soc.defaults);
  if (!soc.fixedPanelWidth) return;
  cfg.panelWidth = soc.fixedPanelWidth;
  cfg.panels = 1;
  if (cfg.panelHeight != soc.fixedPanelHeights[0] && cfg.panelHeight != soc.fixedPanelHeights[1])
    cfg.panelHeight = soc.fixedPanelHeights[1];
}

bool apply(DeviceConfig& cfg, api::JsonReader obj, int& applied, ApplyError& err, Origin origin,
           const pins::SocProfile& soc) {
  applied = 0;
  cfgrules::ConfigError cerr;
  // A restore is allowed to write empty strings to clear a field; an interactive edit is not,
  // because a blank box in the UI means "leave it alone".
  const bool allowEmptyClears = origin == Origin::Restore;
  const bool pinsKept = origin == Origin::Restore && soc.fixedWiring;
  if (!cfgrules::validateSystemRead(obj, cerr, allowEmptyClears, soc, pinsKept)) {
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
    keepFixedWiring(merged, cfg, soc);
  }
  const cfgrules::IpSplit split = cfgrules::systemIpSplit(obj);
  if (split.present) {
    merged.ip = split.ip;
    merged.subnet = split.subnet;
    ++applied;
  }
  if (!cfgrules::validateMatrixGeometry(merged.panelWidth, merged.panels, cerr) ||
      !cfgrules::validateFixedPanel(soc, merged.panelWidth, merged.panels, merged.panelHeight,
                                    cerr) ||
      !cfgrules::validateBrightnessWindow(merged.minBrightness, merged.maxBrightness, cerr) ||
      !cfgrules::validateStaticNet(merged.netStatic, merged.ip, merged.subnet, cerr) ||
      !cfgrules::validateMqttGate(merged.mqttEnabled, merged.mqttHost, cerr) ||
      !cfgrules::validateAuthGate(merged.authEnabled, merged.authUser, merged.authPass, cerr) ||
      !cfgrules::validateAudioPins(merged.pinI2sBclk, merged.pinI2sLrclk, merged.pinI2sDout,
                                   merged.pinI2sMclk, merged.pinAmpEnable, cerr)) {
    err = {422, "validationFailed", cerr.message, cerr.field};
    return false;
  }
  if (soc.fixedPanelWidth && !sameWiring(merged, cfg)) {
    err = {422, "validationFailed", std::string(soc.label) + ": the panel wiring is fixed",
           "panelWiring"};
    return false;
  }
  std::string pinErr;
  if (!pins::validate(merged.pinSet(), soc, pinErr)) {
    err = {400, "invalidPinConfig", pinErr, ""};
    return false;
  }
  cfg = merged;
  return true;
}

}
}
