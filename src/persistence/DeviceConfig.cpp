#include "persistence/DeviceConfig.h"

#include <Preferences.h>
#include <nvs.h>

#include "persistence/DeviceConfigRows.h"

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
void cfgGet(Preferences& p, const char* k, PanelColorOrder& v) {
  v = static_cast<PanelColorOrder>(p.getInt(k, static_cast<int>(v)));
}

template <typename T>
__attribute__((noinline)) bool cfgSame(Preferences& p, const char* k, const T& expected) {
  T stored = expected;
  cfgGet(p, k, stored);
  return p.isKey(k) && stored == expected;
}

// Preferences' on-flash types, written through NVS with its actual status.
bool cfgPut(nvs_handle_t h, const char* k, bool v) { return nvs_set_u8(h, k, v ? 1 : 0) == ESP_OK; }
bool cfgPut(nvs_handle_t h, const char* k, int v) { return nvs_set_i32(h, k, v) == ESP_OK; }
bool cfgPut(nvs_handle_t h, const char* k, long v) { return nvs_set_i32(h, k, v) == ESP_OK; }
bool cfgPut(nvs_handle_t h, const char* k, float v) { return nvs_set_blob(h, k, &v, sizeof(v)) == ESP_OK; }
bool cfgPut(nvs_handle_t h, const char* k, uint8_t v) { return nvs_set_u8(h, k, v) == ESP_OK; }
bool cfgPut(nvs_handle_t h, const char* k, uint16_t v) { return nvs_set_u16(h, k, v) == ESP_OK; }
bool cfgPut(nvs_handle_t h, const char* k, uint32_t v) { return nvs_set_u32(h, k, v) == ESP_OK; }
bool cfgPut(nvs_handle_t h, const char* k, const std::string& v) { return nvs_set_str(h, k, v.c_str()) == ESP_OK; }
bool cfgPut(nvs_handle_t h, const char* k, PanelStart v) { return cfgPut(h, k, static_cast<int>(v)); }
bool cfgPut(nvs_handle_t h, const char* k, Wiring v) { return cfgPut(h, k, static_cast<int>(v)); }
bool cfgPut(nvs_handle_t h, const char* k, PanelColorOrder v) {
  return cfgPut(h, k, static_cast<int>(v));
}

// Matrix geometry keys from older firmware. Nothing reads them any more; save() deletes them so
// they stop occupying entries in the NVS partition.
const char* const kLegacyMatrixKeys[] = {"mwidth", "matlay", "mtilew", "morient", "mserp",
                                         "mflipx", "mflipy", "ph",     "pnx",     "pny",
                                         "cserp",  "pheight"};
}

// Every key defaults to whatever the member already holds, so a config written by an older
// firmware simply leaves the newer fields at their compiled-in defaults.
void DeviceConfig::load() {
  Preferences p;
  p.begin(kNs, true);
#define X(member, key, secret, need) cfgGet(p, key, member);
  AWTRIX_CFG_FIELDS(X)
#undef X
  p.end();
}

bool DeviceConfig::save() const {
  nvs_handle_t handle;
  if (nvs_open(kNs, NVS_READWRITE, &handle) != ESP_OK) {
    persistencePending = true;
    return false;
  }
  bool saved = true;
  for (const auto& row : configfields::kRows)
    if (!configfields::visit(*this, row, [handle](const char* key, const auto& value) {
          return cfgPut(handle, key, value);
        })) saved = false;
  for (const char* legacy : kLegacyMatrixKeys) {
    const esp_err_t status = nvs_erase_key(handle, legacy);
    if (status != ESP_OK && status != ESP_ERR_NVS_NOT_FOUND) saved = false;
  }
  if (nvs_commit(handle) != ESP_OK) saved = false;
  nvs_close(handle);
  // Confirm the values through a fresh handle before acknowledging durable configuration.
  Preferences p;
  if (!p.begin(kNs, true)) { persistencePending = true; return false; }
  for (const auto& row : configfields::kRows)
    if (!configfields::visit(*this, row, [&p](const char* key, const auto& value) {
          return cfgSame(p, key, value);
        })) saved = false;
  p.end();
  persistencePending = !saved;
  return saved;
}


}
