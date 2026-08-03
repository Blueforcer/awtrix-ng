#pragma once

#include <string>

#include "core/SocProfile.h"

namespace awtrix {
namespace pins {

namespace detail {

inline std::string rangeText(const RangeList& list) {
  std::string out;
  for (std::size_t i = 0; i < list.count; ++i) {
    if (!out.empty()) out += ", ";
    out += std::to_string(list.items[i].lo);
    if (list.items[i].hi != list.items[i].lo) out += "-" + std::to_string(list.items[i].hi);
  }
  return out;
}

inline std::string pinListText(const PinList& list) {
  std::string out;
  for (std::size_t i = 0; i < list.count; ++i) {
    if (!out.empty()) out += ",";
    out += std::to_string(list.items[i]);
  }
  return out;
}

}

inline bool isMatrixPin(int pin, const SocProfile& soc) { return soc.matrix.contains(pin); }

inline bool exists(int pin, const SocProfile& soc) {
  return pin >= 0 && pin <= soc.gpioMax && !soc.missing.contains(pin);
}

inline bool isRtcWakePin(int pin, const SocProfile& soc) {
  return exists(pin, soc) && soc.rtc.contains(pin);
}

// Checks a whole pin set at once and returns the first problem as a message meant for the user.
// -1 means the peripheral is off, so only pinMatrix is ever required.
inline bool validate(const PinSet& p, const SocProfile& soc, std::string& err) {
  struct Entry {
    const char* name;
    int pin;
    bool enabled;
    // needsOutput excludes the input-only GPIOs; the analog inputs are happy there.
    bool needsOutput;
  };
  const Entry entries[] = {
      {"pinMatrix", p.matrix, true, true},
      {"pinBtnLeft", p.btnLeft, p.btnLeft >= 0, true},
      {"pinBtnSelect", p.btnSelect, p.btnSelect >= 0, true},
      {"pinBtnRight", p.btnRight, p.btnRight >= 0, true},
      {"pinBattery", p.battery, p.battery >= 0, false},
      {"pinLdr", p.ldr, p.ldr >= 0, false},
      {"pinBuzzer", p.buzzer, p.buzzer >= 0, true},
      {"pinI2cSda", p.i2cSda, p.i2cSda >= 0, true},
      {"pinI2cScl", p.i2cScl, p.i2cScl >= 0, true},
      {"pinDfRx", p.dfRx, p.dfRx >= 0, false},
      {"pinDfTx", p.dfTx, p.dfTx >= 0, true},
      {"pinI2sBclk", p.i2sBclk, p.i2sBclk >= 0, true},
      {"pinI2sLrclk", p.i2sLrclk, p.i2sLrclk >= 0, true},
      {"pinI2sDout", p.i2sDout, p.i2sDout >= 0, true},
  };

  if (!isMatrixPin(p.matrix, soc)) {
    err = "pinMatrix: unsupported pin (compiled drivers: " + detail::pinListText(soc.matrix) + ")";
    return false;
  }
  for (const Entry& e : entries) {
    if (!e.enabled) continue;
    if (!exists(e.pin, soc)) {
      err = std::string(e.name) + ": not a valid " + soc.label + " GPIO (0-" +
            std::to_string(soc.gpioMax);
      if (!soc.missing.empty()) err += " except " + detail::rangeText(soc.missing);
      err += ", or -1 = disabled)";
      return false;
    }
    if (const ReservedRange* r = soc.reserved.find(e.pin)) {
      err = std::string(e.name) + ": GPIO " + std::to_string(r->lo) + "-" + std::to_string(r->hi) +
            " are reserved for " + r->why;
      return false;
    }
    if (e.needsOutput && soc.inputOnly.contains(e.pin)) {
      err = std::string(e.name) + ": GPIO " + detail::rangeText(soc.inputOnly) + " are input-only";
      return false;
    }
  }
  const std::string adcText = detail::rangeText(soc.adc1);
  if (p.battery >= 0 && !soc.adc1.contains(p.battery)) {
    err = "pinBattery: must be an ADC1 pin (GPIO " + adcText + ", usable while WiFi is on)";
    return false;
  }
  if (p.ldr >= 0 && !soc.adc1.contains(p.ldr)) {
    err = "pinLdr: must be an ADC1 pin (GPIO " + adcText + ", usable while WiFi is on)";
    return false;
  }
  for (std::size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
    if (!entries[i].enabled) continue;
    for (std::size_t j = i + 1; j < sizeof(entries) / sizeof(entries[0]); ++j) {
      if (!entries[j].enabled) continue;
      if (entries[i].pin == entries[j].pin) {
        err = std::string("duplicate pin ") + std::to_string(entries[i].pin) + " (" +
              entries[i].name + ", " + entries[j].name + ")";
        // Freeing the matrix pin needs both changes in one body, because either half on its own
        // still collides and would be rejected. Hence the worked example in the message.
        if (std::string(entries[i].name) == "pinMatrix" ||
            std::string(entries[j].name) == "pinMatrix")
          err += " - the matrix pin cannot be shared; move the other pin in the SAME request"
                 " (AWTRIX 2: set pinI2cSda to 17 together with pinMatrix 21)";
        return false;
      }
    }
  }
  return true;
}

inline bool validate(const PinSet& p, std::string& err) {
  return validate(p, activeProfile(), err);
}

inline bool isMatrixPin(int pin) { return isMatrixPin(pin, activeProfile()); }

inline bool isRtcWakePin(int pin) { return isRtcWakePin(pin, activeProfile()); }

}
}
