#include "persistence/IconOriginsStore.h"

#include <LittleFS.h>

#include "persistence/VfsFile.h"
#include "persistence/DocumentFile.h"

namespace awtrix::iconorigins {
namespace {
class LittleFsOrigins : public Backend {
 public:
  bool read(std::string& out) override {
    out.clear();
    if (!LittleFS.exists(kPath)) return true;
    File f = LittleFS.open(kPath, "r");
    if (!f || f.size() > kMaxBytes) return false;
    out.resize(f.size());
    const auto count = f.read(reinterpret_cast<uint8_t*>(out.data()), out.size());
    return count == out.size();
  }
  bool writeAtomic(const std::string& json) override {
    if (!LittleFS.exists("/config") && !LittleFS.mkdir("/config")) return false;
    return document::write(kPath, json);
  }
  bool iconExists(const std::string& name) override {
    return validName(name) && fs::isFile("/ICONS/" + name);
  }
};
}
Backend& storage() { static LittleFsOrigins instance; return instance; }
}
