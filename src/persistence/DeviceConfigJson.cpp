#include <cstdint>
#include <string>

#include "core/api/JsonCoerce.h"
#include "persistence/DeviceConfig.h"
#include "persistence/DeviceConfigFields.h"

namespace awtrix {

namespace {

enum class Kind : uint8_t { Str, Bool, Int, Long, U16, U8, Float, Start, Wire };

// One table row per config field: the JSON name, how to convert it, and a pointer-to-member.
// The union keeps the table constexpr, so it lives in flash rather than costing RAM at boot.
struct Row {
  const char* key;
  Kind kind;
  bool secret;
  union {
    std::string DeviceConfig::*s;
    bool DeviceConfig::*b;
    int DeviceConfig::*i;
    long DeviceConfig::*l;
    uint16_t DeviceConfig::*u16;
    uint8_t DeviceConfig::*u8;
    float DeviceConfig::*f;
    PanelStart DeviceConfig::*ps;
    Wiring DeviceConfig::*wi;
  };

  constexpr Row(const char* k, bool sec, std::string DeviceConfig::*m)
      : key(k), kind(Kind::Str), secret(sec), s(m) {}
  constexpr Row(const char* k, bool sec, bool DeviceConfig::*m)
      : key(k), kind(Kind::Bool), secret(sec), b(m) {}
  constexpr Row(const char* k, bool sec, int DeviceConfig::*m)
      : key(k), kind(Kind::Int), secret(sec), i(m) {}
  constexpr Row(const char* k, bool sec, long DeviceConfig::*m)
      : key(k), kind(Kind::Long), secret(sec), l(m) {}
  constexpr Row(const char* k, bool sec, uint16_t DeviceConfig::*m)
      : key(k), kind(Kind::U16), secret(sec), u16(m) {}
  constexpr Row(const char* k, bool sec, uint8_t DeviceConfig::*m)
      : key(k), kind(Kind::U8), secret(sec), u8(m) {}
  constexpr Row(const char* k, bool sec, float DeviceConfig::*m)
      : key(k), kind(Kind::Float), secret(sec), f(m) {}
  constexpr Row(const char* k, bool sec, PanelStart DeviceConfig::*m)
      : key(k), kind(Kind::Start), secret(sec), ps(m) {}
  constexpr Row(const char* k, bool sec, Wiring DeviceConfig::*m)
      : key(k), kind(Kind::Wire), secret(sec), wi(m) {}
};

// The API key is the stringified member name, not the short NVS key — the wire format stays
// readable while flash keeps the abbreviations.
constexpr Row kRows[] = {
#define X(m, key, secret) Row(#m, (secret) != 0, &DeviceConfig::m),
    AWTRIX_CFG_FIELDS(X)
#undef X
};

}

void DeviceConfig::write(api::JsonWriter& w, bool withSecrets) const {
  for (const Row& r : kRows) {
    if (!withSecrets && r.secret) continue;
    switch (r.kind) {
      case Kind::Str: w.member(r.key, this->*r.s); break;
      case Kind::Bool: w.member(r.key, this->*r.b); break;
      case Kind::Int: w.member(r.key, this->*r.i); break;
      case Kind::Long: w.member(r.key, this->*r.l); break;
      case Kind::U16: w.member(r.key, this->*r.u16); break;
      case Kind::U8: w.member(r.key, this->*r.u8); break;
      case Kind::Float: w.member(r.key, this->*r.f); break;
      case Kind::Start: {
        const int idx = static_cast<int>(this->*r.ps);
        w.member(r.key, kPanelStartNames[idx >= 0 && idx < kPanelStartCount ? idx : 0]);
        break;
      }
      case Kind::Wire: {
        const int idx = static_cast<int>(this->*r.wi);
        w.member(r.key, kWiringNames[idx >= 0 && idx < kWiringCount ? idx : 0]);
        break;
      }
    }
  }
}

// Merges the members present in the object into this config and returns how many were applied.
// Unknown keys are skipped; the caller decides whether zero applied fields is an error.
int DeviceConfig::applyRead(api::JsonReader r) {
  if (!r.isObject() || !r.enterObject()) return 0;
  int n = 0;
  while (r.nextMember()) {
    for (const Row& row : kRows) {
      if (!r.keyEquals(row.key)) continue;
      switch (row.kind) {
        case Kind::Str: {
          std::string v;
          // An empty secret means "unchanged": write() omits passwords, so a form round-trip
          // sends them back blank and must not wipe what is stored.
          if (r.isString() && r.appendString(v) && !(row.secret && v.empty())) {
            this->*row.s = v;
            ++n;
          }
          break;
        }
        case Kind::Bool: this->*row.b = api::coerceBool(r); ++n; break;
        case Kind::Int: this->*row.i = api::coerceInt<int>(r); ++n; break;
        case Kind::Long: this->*row.l = api::coerceInt<long>(r); ++n; break;
        case Kind::U16: this->*row.u16 = api::coerceInt<uint16_t>(r); ++n; break;
        case Kind::U8: this->*row.u8 = api::coerceInt<uint8_t>(r); ++n; break;
        case Kind::Float: this->*row.f = api::coerceFloat(r); ++n; break;
        case Kind::Start: {
          std::string v;
          if (r.isString() && r.appendString(v)) {
            const int idx = enumIndexByName(kPanelStartNames, kPanelStartCount, v);
            if (idx >= 0) {
              this->*row.ps = static_cast<PanelStart>(idx);
              ++n;
            }
          }
          break;
        }
        case Kind::Wire: {
          std::string v;
          if (r.isString() && r.appendString(v)) {
            const int idx = enumIndexByName(kWiringNames, kWiringCount, v);
            if (idx >= 0) {
              this->*row.wi = static_cast<Wiring>(idx);
              ++n;
            }
          }
          break;
        }
      }
      break;
    }
    if (!r.skipValue()) break;
  }
  return n;
}

}
