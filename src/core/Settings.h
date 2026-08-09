#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/WeekdayBarConfig.h"
#include "core/payload/ScrollSpec.h"

namespace awtrix {

struct SettingsError {
  std::string field;
  std::string message;
};

struct OptColor {
  uint32_t rgb = 0;
  bool set = false;
  uint32_t valueOr(uint32_t fallback) const { return set ? rgb : fallback; }
};

struct SettingValue {
  enum class Type : uint8_t { None, Bool, Int, Real, Text };

  Type type = Type::None;
  bool b = false;
  long i = 0;
  float f = 0.0f;
  const char* s = nullptr;

  bool has() const { return type != Type::None; }

  static SettingValue none() { return SettingValue{}; }
  static SettingValue ofBool(bool v) {
    SettingValue o;
    o.type = Type::Bool;
    o.b = v;
    return o;
  }
  static SettingValue ofInt(long v) {
    SettingValue o;
    o.type = Type::Int;
    o.i = v;
    return o;
  }
  static SettingValue ofReal(float v) {
    SettingValue o;
    o.type = Type::Real;
    o.f = v;
    return o;
  }
  static SettingValue ofText(const char* v) {
    SettingValue o;
    o.type = Type::Text;
    o.s = v;
    return o;
  }
};

constexpr int kSepSteady = 0, kSepBlink = 1, kSepPulse = 2;
constexpr int kDateOrderDMY = 0, kDateOrderMDY = 1, kDateOrderYMD = 2;
constexpr int kDateSepDot = 0, kDateSepSlash = 1, kDateSepDash = 2;
constexpr int kYearNone = 0, kYearTwoDigit = 1, kYearFourDigit = 2;

// The defaults below are also what a settings reset restores. JSON key names, ranges and the
// validation rules for every field live in the Field table in Settings.cpp.
struct Settings {
  bool autoBrightness = false;
  int brightness = 120;
  bool autoTransition = true;
  uint32_t textColor = 0xFFFFFFu;
  // Index into kTransitionNames; 19 is Rain. 0 (Random) picks a fresh effect per transition.
  int transitionEffect = 19;
  int transitionDurationMs = 1000;
  long appDurationMs = 7000;
  int timeMode = 1;
  uint32_t calendarHeaderColor = 0xFF0000u;
  uint32_t calendarTextColor = 0x000000u;
  uint32_t calendarBodyColor = 0xFFFFFFu;
  bool time24h = true;
  bool timeLeadingZero = true;
  bool timeShowSeconds = false;
  bool timeShowAmPm = false;
  int timeSeparatorMode = kSepPulse;
  int dateOrder = kDateOrderDMY;
  int dateSeparator = kDateSepDot;
  int dateYearMode = kYearTwoDigit;
  bool dateShowWeekday = false;
  bool dateMonthNames = false;
  bool useCelsius = true;
  bool blockNavigation = false;
  bool soundEnabled = true;
  bool uppercase = true;
  WeekdayBarConfig weekdayBar;
  OptColor timeColor;
  OptColor dateColor;
  OptColor humidityColor;
  OptColor temperatureColor;
  OptColor batteryColor;
  ScrollDefaults scrollDefaults;
  // One gain per output, all of them a percentage. The buzzer default is the old 25-of-30 in the
  // new scale, so no device gets quieter across the update.
  int buzzerVolume = 80;
  int dfplayerVolume = 80;
  // Stored MP3s sit above the stream on purpose: a doorbell has to carry over a station that was
  // deliberately turned down, and files are mastered hotter than a stream anyway.
  int mp3Volume = 70;
  int radioVolume = 60;
  bool radioMeta = true;
  int saturation = 100;
  float gamma = 1.9f;
  OptColor colorCorrection;
  OptColor colorTint;

  void writeMembers(api::JsonWriter& w) const;
  int applyRead(api::JsonReader r);
  static bool validateRead(api::JsonReader r, SettingsError& err);
  SettingValue read(std::string_view key) const;
  static const char* canonicalKey(std::string_view key);
};

}
