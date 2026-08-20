#include "persistence/DeviceConfig.h"

#include <Preferences.h>

#include "persistence/DeviceConfigFields.h"

namespace awtrix {


namespace {
const char* kNs = "awtrix-cfg";

void cfgGet(Preferences& p, const char* k, bool& v) { v = p.getBool(k, v); }
void cfgGet(Preferences& p, const char* k, int& v) { v = p.getInt(k, v); }
void cfgGet(Preferences& p, const char* k, long& v) { v = p.getLong(k, v); }
void cfgGet(Preferences& p, const char* k, float& v) { v = p.getFloat(k, v); }
void cfgGet(Preferences& p, const char* k, uint8_t& v) { v = p.getUChar(k, v); }
void cfgGet(Preferences& p, const char* k, uint16_t& v) { v = p.getUShort(k, v); }
void cfgGet(Preferences& p, const char* k, uint32_t& v) { v = p.getUInt(k, v); }
void cfgGet(Preferences& p, const char* k, std::string& v) {
  v = p.getString(k, v.c_str()).c_str();
}
void cfgGet(Preferences& p, const char* k, PanelStart& v) {
  v = static_cast<PanelStart>(p.getInt(k, static_cast<int>(v)));
}
void cfgGet(Preferences& p, const char* k, Wiring& v) {
  v = static_cast<Wiring>(p.getInt(k, static_cast<int>(v)));
}

void cfgPut(Preferences& p, const char* k, bool v) { p.putBool(k, v); }
void cfgPut(Preferences& p, const char* k, int v) { p.putInt(k, v); }
void cfgPut(Preferences& p, const char* k, long v) { p.putLong(k, v); }
void cfgPut(Preferences& p, const char* k, float v) { p.putFloat(k, v); }
void cfgPut(Preferences& p, const char* k, uint8_t v) { p.putUChar(k, v); }
void cfgPut(Preferences& p, const char* k, uint16_t v) { p.putUShort(k, v); }
void cfgPut(Preferences& p, const char* k, uint32_t v) { p.putUInt(k, v); }
void cfgPut(Preferences& p, const char* k, const std::string& v) { p.putString(k, v.c_str()); }
void cfgPut(Preferences& p, const char* k, PanelStart v) { p.putInt(k, static_cast<int>(v)); }
void cfgPut(Preferences& p, const char* k, Wiring v) { p.putInt(k, static_cast<int>(v)); }

// Matrix geometry keys from older firmware. Nothing reads them any more; save() deletes them so
// they stop occupying entries in the NVS partition.
const char* const kLegacyMatrixKeys[] = {"mwidth", "matlay", "mtilew", "morient", "mserp",
                                         "mflipx", "mflipy", "ph",     "pnx",     "pny",
                                         "cserp"};
}

// Every key defaults to whatever the member already holds, so a config written by an older
// firmware simply leaves the newer fields at their compiled-in defaults.
void DeviceConfig::load() {
  Preferences p;
  if (!p.begin(kNs, true)) return;
#define X(member, key, secret) cfgGet(p, key, member);
  AWTRIX_CFG_FIELDS(X)
#undef X
  p.end();
}

void DeviceConfig::save() const {
  Preferences p;
  if (!p.begin(kNs, false)) return;
#define X(member, key, secret) cfgPut(p, key, member);
  AWTRIX_CFG_FIELDS(X)
#undef X
  for (const char* legacy : kLegacyMatrixKeys)
    if (p.isKey(legacy)) p.remove(legacy);
  p.end();
}


}
