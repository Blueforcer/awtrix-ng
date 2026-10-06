#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "core/SocProfile.h"
#include "core/api/JsonReader.h"
#include "core/render/MatrixLayout.h"

namespace awtrix {
namespace cfgrules {

struct ConfigError {
  std::string field;
  std::string message;
};

namespace detail {

struct NumRange {
  const char* key;
  double lo;
  double hi;
  bool integer;
};

// The accepted range for every numeric key in /api/v1/system. Anything not listed here is not
// range-checked at all, so a new setting needs a row or it goes through unvalidated.
inline const NumRange* ranges(std::size_t& count) {
  static const NumRange kRanges[] = {
      {"mqttPort", 1, 65535, true},
      {"wifiConnectTimeout", 5000, 120000, true},
      {"wifiRoamRssi", -90, 0, true},
      {"webPort", 0, 65535, true},
      {"statsInterval", 1000, 600000, true},
      {"tempDecimals", 0, 2, true},
      {"lowBatteryThreshold", 0, 100, true},
      {"minBrightness", 0, 255, true},
      {"maxBrightness", 0, 255, true},
      {"panelWidth", 1, kMatrixWidthMax, true},
      {"panels", 1, kMatrixWidthMax, true},
      {"tempOffset", -20, 20, false},
      {"humOffset", -50, 50, false},
      {"batteryDividerRatio", 0.1, 10, false},
      {"ldrFactor", 0, 10, false},
      {"ldrGamma", 0.1, 10, false},
      {"brightnessSmoothing", 0, 60000, true},
  };
  count = sizeof(kRanges) / sizeof(kRanges[0]);
  return kRanges;
}

// Anything named pinSomething is checked as a GPIO against the active SoC profile, so a new pin
// setting is validated without being listed anywhere.
inline bool isPinKey(const std::string& k) {
  return k.size() > 3 && k.compare(0, 3, "pin") == 0;
}

struct EnumRule {
  const char* key;
  const char* const* names;
  int count;
};

inline const EnumRule* findEnum(const std::string& key) {
  static const EnumRule kEnums[] = {
      {"panelStart", kPanelStartNames, kPanelStartCount},
      {"panelWiring", kWiringNames, kWiringCount},
      {"panelColorOrder", kPanelColorOrderNames, kPanelColorOrderCount},
  };
  for (const EnumRule& e : kEnums)
    if (key == e.key) return &e;
  return nullptr;
}

inline bool isLayoutBoolKey(const std::string& key) {
  return key == "panelSerpentine" || key == "panelChainReverse" ||
         key == "panelChainSerpentine" || key == "mirror" || key == "rotate";
}

struct ClearRoute {
  const char* key;
  const char* how;
};

// Keys where an empty string would look like a way to erase something but is not; the request is
// refused and pointed at the endpoint that really does it.
inline const ClearRoute* clearRoutes(std::size_t& count) {
  static const ClearRoute kRoutes[] = {
      {"wifiSsid", "device/factory-reset"},
  };
  count = sizeof(kRoutes) / sizeof(kRoutes[0]);
  return kRoutes;
}

inline bool parseDottedQuad(const char* s, std::uint32_t& out) {
  std::uint32_t address = 0;
  int octets = 0;
  while (true) {
    const char* start = s;
    int digits = 0, value = 0;
    while (*s >= '0' && *s <= '9') {
      if (++digits > 3) return false;
      value = value * 10 + (*s - '0');
      ++s;
    }
    if (digits == 0 || value > 255 || (digits > 1 && *start == '0')) return false;
    address = (address << 8) | static_cast<std::uint32_t>(value);
    ++octets;
    if (*s == '.' && octets < 4) {
      ++s;
      continue;
    }
    if (*s != '\0' || octets != 4) return false;
    out = address;
    return true;
  }
}

inline bool isDottedQuad(const char* s) {
  std::uint32_t ignored = 0;
  return parseDottedQuad(s, ignored);
}

inline bool isHostAddress(std::uint32_t address) {
  const std::uint32_t first = address >> 24;
  return first != 0 && first != 127 && first < 224;
}

inline int cidrPrefix(const char* s) {
  if (*s != '/') return -1;
  ++s;
  int digits = 0, value = 0;
  while (*s >= '0' && *s <= '9') {
    if (++digits > 2) return -1;
    value = value * 10 + (*s - '0');
    ++s;
  }
  if (digits == 0 || *s != '\0' || value > 32) return -1;
  // Reject a padded "/04": two digits below ten mean a leading zero, which is not a prefix we want
  // to silently accept.
  if (digits == 2 && value < 10) return -1;
  return value;
}

inline const char* const* ipKeys(std::size_t& count) {
  static const char* kKeys[] = {"ip", "gateway", "subnet", "dns1", "dns2"};
  count = sizeof(kKeys) / sizeof(kKeys[0]);
  return kKeys;
}

inline const NumRange* findRange(const std::string& key) {
  std::size_t n = 0;
  const NumRange* r = ranges(n);
  for (std::size_t i = 0; i < n; ++i)
    if (key == r[i].key) return &r[i];
  return nullptr;
}

inline const ClearRoute* findClearRoute(const std::string& key) {
  std::size_t n = 0;
  const ClearRoute* c = clearRoutes(n);
  for (std::size_t i = 0; i < n; ++i)
    if (key == c[i].key) return &c[i];
  return nullptr;
}

inline bool isIpKey(const std::string& key) {
  std::size_t n = 0;
  const char* const* k = ipKeys(n);
  for (std::size_t i = 0; i < n; ++i)
    if (key == k[i]) return true;
  return false;
}

inline bool asString(api::JsonReader r, std::string& out) {
  return r.isString() && r.appendString(out);
}

}

struct IpSplit {
  bool present = false;
  std::string ip;
  std::string subnet;
};

// Turns an "ip" of the form 192.168.1.50/24 into the address plus a dotted-quad mask, so the rest
// of the system only deals with a separate ip and subnet. Absent or plain: present stays false.
inline IpSplit systemIpSplit(api::JsonReader obj) {
  IpSplit split;
  std::string v;
  if (!detail::asString(api::memberValue(obj, "ip"), v)) return split;
  const std::size_t slash = v.find('/');
  if (slash == std::string::npos) return split;
  const int prefix = detail::cidrPrefix(v.c_str() + slash);
  if (prefix < 0 || slash == 0 || slash >= 16) return split;
  const std::uint32_t mask = prefix == 0 ? 0u : 0xFFFFFFFFu << (32 - prefix);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", static_cast<unsigned>(mask >> 24),
                static_cast<unsigned>((mask >> 16) & 0xFF),
                static_cast<unsigned>((mask >> 8) & 0xFF), static_cast<unsigned>(mask & 0xFF));
  split.present = true;
  split.ip = v.substr(0, slash);
  split.subnet = buf;
  return split;
}

// The fields are dotted quads or empty already; this checks that they form a usable address.
// 0.0.0.0 as gateway or DNS server means none.
inline bool validateStaticNet(bool netStatic, const std::string& ip, const std::string& subnet,
                              const std::string& gateway, const std::string& dns1, const std::string& dns2,
                              ConfigError& err) {
  if (!netStatic || ip.empty()) return true;
  if (subnet.empty()) {
    err = {"subnet", "needs a mask"};
    return false;
  }
  std::uint32_t address = 0, mask = 0;
  detail::parseDottedQuad(subnet.c_str(), mask);
  if (mask == 0 || (~mask & (~mask + 1)) != 0) {
    err = {"subnet", "not a mask"};
    return false;
  }
  detail::parseDottedQuad(ip.c_str(), address);
  const std::uint32_t host = address & ~mask;
  if (!detail::isHostAddress(address) || (~mask > 1 && (host == 0 || host == ~mask))) {
    err = {"ip", "not a host address"};
    return false;
  }
  const std::pair<const char*, const std::string*> servers[] = {
      {"gateway", &gateway}, {"dns1", &dns1}, {"dns2", &dns2}};
  for (const auto& server : servers) {
    std::uint32_t value = 0;
    if (server.second->empty() || !detail::parseDottedQuad(server.second->c_str(), value) || value == 0) continue;
    if (!detail::isHostAddress(value) || value == address) {
      err = {server.first, "not a host address"};
      return false;
    }
  }
  return true;
}

inline bool validateBrightnessWindow(int minBrightness, int maxBrightness, ConfigError& err) {
  if (minBrightness > maxBrightness) {
    err = {"minBrightness", "must be <= maxBrightness"};
    return false;
  }
  return true;
}

inline bool validateMqttGate(bool mqttEnabled, const std::string& mqttHost, ConfigError& err) {
  if (mqttEnabled && mqttHost.empty()) {
    err = {"mqttHost", "set mqttHost first"};
    return false;
  }
  return true;
}

inline bool validateAuthGate(bool authEnabled, const std::string& authUser,
                             const std::string& authPass, ConfigError& err) {
  if (authEnabled && (authUser.empty() || authPass.empty())) {
    err = {"authUser", "set authUser and authPass first"};
    return false;
  }
  return true;
}

// panelWidth x panels is the total width; the panels are always 8 high.
inline bool validateMatrixGeometry(int panelWidth, int panels, ConfigError& err) {
  if (espMatrixWidth(panelWidth, panels) == 0) {
    err = {"panelWidth", "width must be " + std::to_string(kEspDisplayLimits.minWidth) + ".." +
                             std::to_string(kEspDisplayLimits.maxWidth)};
    return false;
  }
  return true;
}

// bclk/lrclk/dout are the bus and go together; mclk and ampEnable are extras some DAC boards
// need and are only meaningful once the bus itself is wired.
inline bool validateAudioPins(int bclk, int lrclk, int dout, int mclk, int ampEnable,
                              ConfigError& err) {
  const int set = (bclk >= 0) + (lrclk >= 0) + (dout >= 0);
  if (set != 0 && set != 3) {
    const char* missing = bclk < 0 ? "pinI2sBclk" : (lrclk < 0 ? "pinI2sLrclk" : "pinI2sDout");
    err = {missing, "set all three I2S pins or none"};
    return false;
  }
  if (set == 0 && mclk >= 0) {
    err = {"pinI2sMclk", "needs the three I2S pins"};
    return false;
  }
  if (set == 0 && ampEnable >= 0) {
    err = {"pinAmpEnable", "needs the three I2S pins"};
    return false;
  }
  return true;
}

// Walks a /api/v1/system body and stops at the first key that breaks a rule. Keys that match none
// of the rules are accepted untouched, which is what lets plain strings and flags pass through.
// allowEmptyClears is for restoring a backup, where an empty wifiSsid is a real recorded value.
// ignore names keys the caller drops unread, such as fields the platform does not offer.
inline bool validateSystemRead(api::JsonReader obj, ConfigError& err,
                               bool allowEmptyClears = false,
                               bool (*ignore)(std::string_view, const void*) = nullptr,
                               const void* context = nullptr) {
  if (!obj.isObject()) return true;
  std::string subnetValue;
  const bool hasSubnet = detail::asString(api::memberValue(obj, "subnet"), subnetValue);

  api::JsonReader r = obj;
  if (!r.enterObject()) return true;
  while (r.nextMember()) {
    const std::string key(r.key());
    if (ignore && ignore(key, context)) {
      if (!r.skipValue()) return false;
      continue;
    }
    if (const detail::NumRange* nr = detail::findRange(key)) {
      long long i = 0;
      if (nr->integer && (!r.isNumber() || !r.isInteger() || !r.asLong(i) || i < LONG_MIN ||
                          i > LONG_MAX)) {
        err = {nr->key, "must be an integer"};
        return false;
      }
      if (!nr->integer && !r.isNumber()) {
        err = {nr->key, "must be a number"};
        return false;
      }
      double d = 0.0;
      if (!r.asDouble(d) || d < nr->lo || d > nr->hi) {
        err = {nr->key, "out of range"};
        return false;
      }
    } else if (const detail::EnumRule* er = detail::findEnum(key)) {
      std::string v;
      if (!detail::asString(r, v) || enumIndexByName(er->names, er->count, v) < 0) {
        err = {key, enumNameChoices(er->names, er->count)};
        return false;
      }
    } else if (detail::isLayoutBoolKey(key)) {
      if (!r.isBool()) {
        err = {key, "must be a boolean"};
        return false;
      }
    } else if (!allowEmptyClears && detail::findClearRoute(key)) {
      std::string v;
      if (detail::asString(r, v) && v.empty()) {
        err = {key, std::string("cannot be empty; use ") + detail::findClearRoute(key)->how};
        return false;
      }
    } else if (detail::isIpKey(key)) {
      std::string v;
      if (!detail::asString(r, v)) {
        err = {key, "must be a string"};
        return false;
      }
      if (!v.empty() && !detail::isDottedQuad(v.c_str())) {
        const std::size_t slash = v.find('/');
        bool accepted = false;
        if (key == "ip" && slash != std::string::npos && slash > 0 && slash < 16 &&
            detail::cidrPrefix(v.c_str() + slash) >= 0 &&
            detail::isDottedQuad(v.substr(0, slash).c_str())) {
          if (hasSubnet && !subnetValue.empty()) {
            err = {"subnet",
                   "mask already set on ip"};
            return false;
          }
          accepted = true;
        }
        if (!accepted) {
          if (key == "ip") {
            err = {key,
                   "must be IPv4, IPv4/prefix or \"\""};
          } else {
            err = {key, "must be IPv4 or \"\""};
          }
          return false;
        }
      }
    } else if (detail::isPinKey(key)) {
      long long p = 0;
      if (!r.isNumber() || !r.isInteger() || !r.asLong(p) || p < LONG_MIN || p > LONG_MAX) {
        err = {key, "must be a GPIO number"};
        return false;
      }
      const int gpioMax = pins::activeProfile().gpioMax;
      if (p < -1 || p > gpioMax) {
        err = {key, "must be -1 or 0.." + std::to_string(gpioMax)};
        return false;
      }
    }
    if (!r.skipValue()) return false;
  }
  return r.ok();
}

}
}
