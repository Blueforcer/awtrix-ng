
#include "core/CoreEngine.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "persistence/AppOrderStore.h"
#include "persistence/RadioStore.h"
#include "persistence/DeviceConfig.h"
#include "persistence/NvsSettings.h"
#include "sim/SimStore.h"

namespace awtrix {

// Host half of every persistence seam: the device keeps settings in NVS and the rest on SPIFFS,
// here they are all plain JSON files under the data directory so they can be edited by hand.
namespace nvs {

void loadSettings(Settings& s) {
  std::string blob;
  if (!sim::readFile(sim::hostPath("/settings.json"), blob) || blob.empty()) return;
  if (!api::isWellFormed(blob)) return;
  s.applyRead(api::JsonReader(blob));
}

void saveSettings(const Settings& s) {
  std::string out;
  out.reserve(1536);
  api::JsonWriter w(out);
  w.beginObject();
  s.writeMembers(w);
  w.member("schemaVersion", 1);
  w.endObject();
  sim::writeFile(sim::hostPath("/settings.json"), out);
}

}

void DeviceConfig::load() {
  std::string blob;
  if (!sim::readFile(sim::hostPath("/device.json"), blob) || blob.empty()) return;
  if (!api::isWellFormed(blob)) return;
  applyRead(api::JsonReader(blob));
}

void DeviceConfig::save() const {
  std::string out;
  out.reserve(1536);
  api::JsonWriter w(out);
  w.beginObject();
  write(w, true);
  w.endObject();
  sim::writeFile(sim::hostPath("/device.json"), out);
}

namespace apporder {

void save(const std::string& json) { sim::writeFile(sim::hostPath("/apploop.json"), json); }

void load(CoreEngine& engine) {
  std::string content;
  if (!sim::readFile(sim::hostPath("/apploop.json"), content) || content.empty()) return;
  engine.setAppOrder(content);
}

}

namespace radiostore {

void save(const std::string& json) { sim::writeFile(sim::hostPath("/radio.json"), json); }

void load(CoreEngine& engine) {
  std::string content;
  if (!sim::readFile(sim::hostPath("/radio.json"), content) || content.empty()) return;
  DispatchDetail detail;
  engine.setStations(content, detail);
}

}
}
