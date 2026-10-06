#include "persistence/IconOriginsStore.h"

#include <filesystem>

#include "platform/linux/host/HostStore.h"
#include "persistence/VfsFile.h"

namespace awtrix::iconorigins {
namespace {
class HostOrigins : public Backend {
 public:
  bool read(std::string& out) override {
    out.clear();
    std::error_code ec;
    const auto path = std::filesystem::u8path(host::hostPath(kPath));
    if (!std::filesystem::exists(path, ec)) return !ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxBytes) return false;
    return host::readFile(host::hostPath(kPath), out);
  }
  bool writeAtomic(const std::string& json) override {
    const auto path = std::filesystem::u8path(host::hostPath(kPath));
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return !ec && host::writeFile(path.u8string(), json);
  }
  bool iconExists(const std::string& name) override {
    if (!validName(name)) return false;
    return fs::isFile("/ICONS/" + name);
  }
};
}
Backend& storage() { static HostOrigins instance; return instance; }
}
