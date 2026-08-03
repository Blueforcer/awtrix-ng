#include "persistence/RadioStore.h"

#include <LittleFS.h>

#include "core/Command.h"
#include "core/CoreEngine.h"

namespace awtrix {
namespace radiostore {

namespace {
constexpr const char* kPath = "/radio.json";
}

void save(const std::string& json) {
  File f = LittleFS.open(kPath, "w");
  if (!f) return;
  f.print(json.c_str());
  f.close();
}

void load(CoreEngine& engine) {
  File f = LittleFS.open(kPath, "r");
  if (!f) return;
  const String content = f.readString();
  f.close();
  DispatchDetail detail;
  engine.setStations(std::string(content.c_str()), detail);
}

}
}
