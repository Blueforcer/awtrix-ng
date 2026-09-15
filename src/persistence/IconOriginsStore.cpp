#include "persistence/IconOriginsStore.h"

#include <LittleFS.h>

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
    constexpr const char* temp = "/config/icon-origins.tmp";
    if (!LittleFS.exists("/config") && !LittleFS.mkdir("/config")) return false;
    File f = LittleFS.open(temp, "w");
    if (!f) return false;
    const bool complete = f.write(reinterpret_cast<const uint8_t*>(json.data()), json.size()) == json.size();
    f.flush(); f.close();
    if (complete && LittleFS.rename(temp, kPath)) return true;
    LittleFS.remove(temp);
    return false;
  }
  bool iconExists(const std::string& name) override {
    if (!validName(name)) return false;
    File f = LittleFS.open(("/ICONS/" + name).c_str(), "r");
    return f && !f.isDirectory();
  }
};
}
Backend& storage() { static LittleFsOrigins instance; return instance; }
}
