#include <cstdint>
#include <string>

#include "core/api/JsonCoerce.h"
#include "persistence/DeviceConfig.h"
#include "persistence/DeviceConfigRows.h"

namespace awtrix {

using configfields::Kind;
using configfields::Row;
using configfields::kRows;

void DeviceConfig::write(api::JsonWriter& w, bool withSecrets) const {
  for (const Row& r : kRows) {
    if ((!withSecrets && r.secret) || !offers(r.need)) continue;
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
      case Kind::ColorOrder: {
        const int idx = static_cast<int>(this->*r.co);
        const int fallback = static_cast<int>(PanelColorOrder::Grb);
        w.member(r.key, kPanelColorOrderNames[idx >= 0 && idx < kPanelColorOrderCount ? idx
                                                                                    : fallback]);
        break;
      }
    }
  }
}

// Merges the members present in the object into this config and returns how many were applied.
// Unknown keys and fields the platform does not offer are skipped; the caller decides whether
// zero applied fields is an error.
int DeviceConfig::applyRead(api::JsonReader r) {
  if (!r.isObject() || !r.enterObject()) return 0;
  int n = 0;
  while (r.nextMember()) {
    for (const Row& row : kRows) {
      if (!r.keyEquals(row.key)) continue;
      if (!offers(row.need)) break;
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
        case Kind::ColorOrder: {
          std::string v;
          if (r.isString() && r.appendString(v)) {
            const int idx =
                enumIndexByName(kPanelColorOrderNames, kPanelColorOrderCount, v);
            if (idx >= 0) {
              this->*row.co = static_cast<PanelColorOrder>(idx);
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
