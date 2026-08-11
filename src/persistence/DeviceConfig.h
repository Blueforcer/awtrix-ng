#pragma once

#include <cstdint>
#include <string>

#include "core/PinRules.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "core/render/MatrixLayout.h"

namespace awtrix {

// Installation-level configuration: wiring, credentials, network. Stored key by key in NVS and
// only changed deliberately, unlike Settings, which the user changes all day and lives as JSON.
struct DeviceConfig {
  std::string wifiSsid;
  std::string wifiPass;
  bool netStatic = false;
  std::string ip, gateway, subnet, dns1, dns2;
  long wifiConnectTimeout = 15000;
  int wifiRoamRssi = 0;

  bool mqttEnabled = false;
  std::string mqttHost;
  uint16_t mqttPort = 1883;
  std::string mqttUser, mqttPass, mqttPrefix;
  bool haDiscovery = false;
  std::string haPrefix = "homeassistant";

  std::string ntpServer = "pool.ntp.org";
  std::string tz = "CET-1CEST,M3.5.0,M10.5.0/3";
  std::string tzName = "Europe/Berlin";

  std::string hostname;
  int webPort = 80;
  bool authEnabled = false;
  std::string authUser, authPass;

  float tempOffset = -9.0f, humOffset = 0.0f;
  float batteryDividerRatio = 1.79f;
  uint8_t lowBatteryThreshold = 0;
  uint8_t minBrightness = 10, maxBrightness = 220;
  float ldrFactor = 1.0f, ldrGamma = 2.2f;
  bool ldrOnGround = false;
  long brightnessSmoothing = 10000;
  int panelWidth = 32;
  int panels = 1;
  PanelStart panelStart = PanelStart::TopLeft;
  Wiring panelWiring = Wiring::Rows;
  bool panelSerpentine = true;
  bool panelChainReverse = false, panelChainSerpentine = false;
  bool mirror = false, rotate = false, swapButtons = false;
  bool dfplayer = false;
  std::string buttonCallback;
  bool artnet = false;
  long statsInterval = 10000;
  uint8_t tempDecimals = 0;
  bool debugMode = false;

  bool scriptingEnabled = true;

  // Defaults come from the SoC profile at construction; load() then overwrites whatever the user
  // has actually stored. A profile change therefore only affects pins nobody has pinned down.
  int pinMatrix = pins::activeProfile().defaults.matrix;
  int pinBtnLeft = pins::activeProfile().defaults.btnLeft;
  int pinBtnSelect = pins::activeProfile().defaults.btnSelect;
  int pinBtnRight = pins::activeProfile().defaults.btnRight;
  int pinBattery = pins::activeProfile().defaults.battery;
  int pinLdr = pins::activeProfile().defaults.ldr;
  int pinBuzzer = pins::activeProfile().defaults.buzzer;
  int pinI2cSda = pins::activeProfile().defaults.i2cSda;
  int pinI2cScl = pins::activeProfile().defaults.i2cScl;
  int pinDfRx = pins::activeProfile().defaults.dfRx;
  int pinDfTx = pins::activeProfile().defaults.dfTx;
  int pinI2sBclk = pins::activeProfile().defaults.i2sBclk;
  int pinI2sLrclk = pins::activeProfile().defaults.i2sLrclk;
  int pinI2sDout = pins::activeProfile().defaults.i2sDout;
  int pinI2sMclk = pins::activeProfile().defaults.i2sMclk;
  int pinAmpEnable = pins::activeProfile().defaults.ampEnable;

  void load();
  void save() const;
  void write(api::JsonWriter& w, bool withSecrets = false) const;
  int applyRead(api::JsonReader r);

  MatrixLayout matrixLayout() const {
    MatrixLayout l;
    l.panelWidth = panelWidth;
    l.panels = panels;
    l.panelStart = panelStart;
    l.panelWiring = panelWiring;
    l.panelSerpentine = panelSerpentine;
    l.panelChainReverse = panelChainReverse;
    l.panelChainSerpentine = panelChainSerpentine;
    l.mirror = mirror;
    l.rotate180 = rotate;
    return sanitizeMatrixLayout(l);
  }

  pins::PinSet pinSet() const {
    pins::PinSet p;
    p.matrix = pinMatrix;
    p.btnLeft = pinBtnLeft; p.btnSelect = pinBtnSelect; p.btnRight = pinBtnRight;
    p.battery = pinBattery; p.ldr = pinLdr; p.buzzer = pinBuzzer;
    p.i2cSda = pinI2cSda; p.i2cScl = pinI2cScl;
    p.dfRx = pinDfRx; p.dfTx = pinDfTx;
    p.i2sBclk = pinI2sBclk; p.i2sLrclk = pinI2sLrclk; p.i2sDout = pinI2sDout;
    p.i2sMclk = pinI2sMclk; p.ampEnable = pinAmpEnable;
    p.dfplayerEnabled = dfplayer;
    return p;
  }
  bool validatePins(std::string& err) const { return pins::validate(pinSet(), err); }
};

}
