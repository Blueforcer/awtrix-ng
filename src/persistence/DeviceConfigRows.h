#pragma once

#include "persistence/DeviceConfig.h"
#include "persistence/DeviceConfigFields.h"

namespace awtrix::configfields {

enum class Kind : uint8_t { Str, Bool, Int, Long, U16, U8, Float, Start, Wire, ColorOrder };

// Config field names, storage keys, types and member pointers.
struct Row {
  const char* key;
  const char* nvsKey;
  Kind kind;
  bool secret;
  ConfigNeed need;
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
    PanelColorOrder DeviceConfig::*co;
  };

  constexpr Row(const char* k, const char* nv, bool sec, ConfigNeed nd, std::string DeviceConfig::*m)
      : key(k), nvsKey(nv), kind(Kind::Str), secret(sec), need(nd), s(m) {}
  constexpr Row(const char* k, const char* nv, bool sec, ConfigNeed nd, bool DeviceConfig::*m)
      : key(k), nvsKey(nv), kind(Kind::Bool), secret(sec), need(nd), b(m) {}
  constexpr Row(const char* k, const char* nv, bool sec, ConfigNeed nd, int DeviceConfig::*m)
      : key(k), nvsKey(nv), kind(Kind::Int), secret(sec), need(nd), i(m) {}
  constexpr Row(const char* k, const char* nv, bool sec, ConfigNeed nd, long DeviceConfig::*m)
      : key(k), nvsKey(nv), kind(Kind::Long), secret(sec), need(nd), l(m) {}
  constexpr Row(const char* k, const char* nv, bool sec, ConfigNeed nd, uint16_t DeviceConfig::*m)
      : key(k), nvsKey(nv), kind(Kind::U16), secret(sec), need(nd), u16(m) {}
  constexpr Row(const char* k, const char* nv, bool sec, ConfigNeed nd, uint8_t DeviceConfig::*m)
      : key(k), nvsKey(nv), kind(Kind::U8), secret(sec), need(nd), u8(m) {}
  constexpr Row(const char* k, const char* nv, bool sec, ConfigNeed nd, float DeviceConfig::*m)
      : key(k), nvsKey(nv), kind(Kind::Float), secret(sec), need(nd), f(m) {}
  constexpr Row(const char* k, const char* nv, bool sec, ConfigNeed nd, PanelStart DeviceConfig::*m)
      : key(k), nvsKey(nv), kind(Kind::Start), secret(sec), need(nd), ps(m) {}
  constexpr Row(const char* k, const char* nv, bool sec, ConfigNeed nd, Wiring DeviceConfig::*m)
      : key(k), nvsKey(nv), kind(Kind::Wire), secret(sec), need(nd), wi(m) {}
  constexpr Row(const char* k, const char* nv, bool sec, ConfigNeed nd, PanelColorOrder DeviceConfig::*m)
      : key(k), nvsKey(nv), kind(Kind::ColorOrder), secret(sec), need(nd), co(m) {}
};

inline constexpr Row kRows[] = {
#define X(m, key, secret, need) Row(#m, key, (secret) != 0, ConfigNeed::need, &DeviceConfig::m),
    AWTRIX_CFG_FIELDS(X)
#undef X
#define PLATFORM_CONFIG(type, member, initial, key, secret, need) \
    Row(#member, key, (secret) != 0, ConfigNeed::need, &DeviceConfig::member),
#include "platform_settings/ConfigFields.inc"
#undef PLATFORM_CONFIG
};

template <typename Fn>
bool visit(const DeviceConfig& cfg, const Row& row, Fn fn) {
  switch (row.kind) {
    case Kind::Str: return fn(row.nvsKey, cfg.*row.s);
    case Kind::Bool: return fn(row.nvsKey, cfg.*row.b);
    case Kind::Int: return fn(row.nvsKey, cfg.*row.i);
    case Kind::Long: return fn(row.nvsKey, cfg.*row.l);
    case Kind::U16: return fn(row.nvsKey, cfg.*row.u16);
    case Kind::U8: return fn(row.nvsKey, cfg.*row.u8);
    case Kind::Float: return fn(row.nvsKey, cfg.*row.f);
    case Kind::Start: return fn(row.nvsKey, cfg.*row.ps);
    case Kind::Wire: return fn(row.nvsKey, cfg.*row.wi);
    case Kind::ColorOrder: return fn(row.nvsKey, cfg.*row.co);
  }
  return false;
}

}
