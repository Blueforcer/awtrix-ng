#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "core/effects/IEffect.h"
#include "core/net/LinkStatus.h"

namespace awtrix {

struct Indicator {
  bool on = false;
  uint32_t color = 0x000000u;
  uint16_t blinkMs = 0;
  uint16_t fadeMs = 0;
};

struct RuntimeState {
  float temperatureC = 0.0f;
  float humidity = 0.0f;
  float pressureHpa = 0.0f;
  float lightLevel = 0.0f;
  uint16_t ldrRaw = 0;
  uint16_t batteryPinMillivolts = 0;
  float batteryVoltage = 0.0f;
  uint8_t batteryPercent = 0;
  bool lowBattery = false;

  // Set once the sensors have been probed at boot. Temperature, humidity and battery decide whether
  // those built-in apps exist at all; pressure and light only feed readings to scripts and the API.
  bool hasTemperature = true;
  bool hasHumidity = true;
  bool hasBattery = true;
  bool hasPressure = false;
  bool hasLightSensor = false;

  std::string currentApp;
  bool matrixOff = false;

  bool radioPlaying = false;
  std::string radioStation;
  std::string radioTitle;
  std::string radioError;
  uint8_t brightnessActual = 120;
  std::string globalOverlay;
  EffectSettings globalOverlaySettings;
  uint8_t tempDecimals = 0;

  bool artnetMode = false;
  bool moodlightMode = false;
  uint32_t moodlightColor = 0xFFFFFFu;
  uint8_t moodlightBrightness = 120;

  std::array<Indicator, 3> indicators{};
  std::array<bool, 3> buttons{};

  long receivedMessages = 0;
  net::LinkStatus wifi;
  net::LinkStatus mqtt;
  uint16_t fps = 0;
};

}
