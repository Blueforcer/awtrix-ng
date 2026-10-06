#include "persistence/AppOrderStore.h"

#include <LittleFS.h>

#include "core/CoreEngine.h"
#include "persistence/DocumentFile.h"

namespace awtrix {
namespace apporder {

namespace {
constexpr const char* kPath = "/apploop.json";
bool unsaved = false;
}

bool pending() { return unsaved; }

void save(const std::string& json) { unsaved = !document::write(kPath, json); }

void load(CoreEngine& engine) {
  File f = LittleFS.open(kPath, "r");
  if (!f) return;
  const String content = f.readString();
  f.close();
  engine.setAppOrder(std::string(content.c_str()));
}

}
}
