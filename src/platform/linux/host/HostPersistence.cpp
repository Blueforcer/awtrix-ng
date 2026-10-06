
#include "core/CoreEngine.h"
#include "core/SettingsBlob.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "persistence/AppOrderStore.h"
#include "persistence/RadioStore.h"
#include "persistence/DeviceConfig.h"
#include "persistence/NvsSettings.h"
#include "platform/linux/host/HostStore.h"
#include "platform/linux/host/HostPersistence.h"

namespace awtrix {

// Host half of every persistence seam: JSON files under the data directory.
namespace nvs {

void loadSettings(Settings& s) {
  std::string blob;
  if (!host::readFile(host::hostPath("/settings.json"), blob)) return;
  settingsblob::apply(s, blob);
}

void saveSettings(const Settings& s) {
  const std::string out = settingsblob::encode(s);
  host::persistence::save(host::persistence::Document::Settings, out);
}

}

void DeviceConfig::load() {
  std::string blob;
  if (!host::readFile(host::hostPath("/device.json"), blob) || blob.empty()) return;
  if (!api::isWellFormed(blob)) return;
  applyRead(api::JsonReader(blob));
}

bool DeviceConfig::save() const {
  std::string out;
  out.reserve(1536);
  api::JsonWriter w(out);
  w.beginObject();
  write(w, true);
  w.endObject();
  persistencePending = !host::persistence::save(host::persistence::Document::DeviceConfig, out);
  return !persistencePending;
}

namespace apporder {

bool pending() { return host::persistence::pending(host::persistence::Document::AppOrder); }

void save(const std::string& json) { host::persistence::save(host::persistence::Document::AppOrder, json); }

void load(CoreEngine& engine) {
  std::string content;
  if (!host::readFile(host::hostPath("/apploop.json"), content) || content.empty()) return;
  engine.setAppOrder(content);
}

}

namespace radiostore {

bool pending() { return host::persistence::pending(host::persistence::Document::Radio); }

void save(const std::string& json) { host::persistence::save(host::persistence::Document::Radio, json); }

void load(CoreEngine& engine) {
  std::string content;
  if (!host::readFile(host::hostPath("/radio.json"), content) || content.empty()) return;
  DispatchDetail detail;
  engine.setStations(content, detail);
}

}
}
