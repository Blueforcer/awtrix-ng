#include "persistence/IconOriginsStore.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

#include "sim/SimStore.h"

namespace awtrix::iconorigins {
namespace {
class HostOrigins : public Backend {
 public:
  bool read(std::string& out) override {
    out.clear();
    std::error_code ec;
    const auto path = std::filesystem::u8path(sim::hostPath(kPath));
    if (!std::filesystem::exists(path, ec)) return !ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxBytes) return false;
    return sim::readFile(sim::hostPath(kPath), out);
  }
  bool writeAtomic(const std::string& json) override {
    const auto path = std::filesystem::u8path(sim::hostPath(kPath));
    const auto temp = std::filesystem::u8path(sim::hostPath("/config/icon-origins.tmp"));
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    {
      std::ofstream file(temp, std::ios::binary | std::ios::trunc);
      if (!file) return false;
      file.write(json.data(), static_cast<std::streamsize>(json.size()));
      file.flush(); file.close();
      if (!file) { std::filesystem::remove(temp, ec); return false; }
    }
#ifdef _WIN32
    const bool ok = MoveFileExW(temp.c_str(), path.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    const bool ok = std::rename(temp.c_str(), path.c_str()) == 0;
#endif
    if (!ok) std::filesystem::remove(temp, ec);
    return ok;
  }
  bool iconExists(const std::string& name) override {
    if (!validName(name)) return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::u8path(sim::hostPath("/ICONS/" + name)), ec);
  }
};
}
Backend& storage() { static HostOrigins instance; return instance; }
}
