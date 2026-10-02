#include "platform/rp2040/LittleFsPreferences.h"
#include <LittleFS.h>
#include "system/Log.h"

namespace awtrix::storage {
namespace {
class Backend final : public KvBackend {
 public:
  Read read(const std::string& name, std::string& bytes) override {
    const auto path = "/NVS/" + name + ".bin";
    if (!LittleFS.exists(path.c_str())) return Read::Missing;
    File f = LittleFS.open(path.c_str(), "r");
    if (!f || f.size() > KvPreferences::kMaxBytes) return Read::Error;
    bytes.resize(f.size());
    return f.read(reinterpret_cast<uint8_t*>(bytes.data()), bytes.size()) == bytes.size()
               ? Read::Ok : Read::Error;
  }
  bool replace(const std::string& name, const std::string& bytes) override {
    if (!LittleFS.exists("/NVS") && !LittleFS.mkdir("/NVS")) return false;
    const auto path = "/NVS/" + name + ".bin";
    const auto temp = path + ".tmp";
    File f = LittleFS.open(temp.c_str(), "w");
    if (!f) return false;
    bool ok = f.write(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()) == bytes.size();
    f.flush(); f.close();
    // Check the staged bytes before publishing. Never delete the old snapshot first.
    f = LittleFS.open(temp.c_str(), "r");
    ok = ok && f && f.size() == bytes.size();
    for (std::size_t i = 0; ok && i < bytes.size(); ++i) ok = f.read() == uint8_t(bytes[i]);
    f.close();
    if (ok && LittleFS.rename(temp.c_str(), path.c_str())) return true;
    LittleFS.remove(temp.c_str());
    return false;
  }
};
}
KvBackend& littleFsKvBackend() { static Backend backend; return backend; }
}
void Preferences::end() {
  if (!store_.end()) awtrix::logf("preferences: namespace commit failed");
}
