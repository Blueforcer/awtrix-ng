#include "sim/SimStore.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "persistence/Filesystem.h"

namespace awtrix {
namespace sim {

namespace {
std::string g_dataDir = "simdata";
}

void setDataDir(const std::string& dir) { g_dataDir = dir; }
const std::string& dataDir() { return g_dataDir; }

std::string hostPath(const std::string& devicePath) { return g_dataDir + devicePath; }

bool readFile(const std::string& hostPath, std::string& out) {
  std::ifstream f(std::filesystem::u8path(hostPath), std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool writeFile(const std::string& hostPath, const std::string& data) {
  std::ofstream f(std::filesystem::u8path(hostPath), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  return f.good();
}

}

namespace fs {

// Recreates the folders the device gets from its flash image, so a fresh checkout has somewhere to
// put uploaded icons, palettes, melodies and scripts.
bool begin() {
  namespace stdfs = std::filesystem;
  std::error_code ec;
  for (const char* d : {"", "/ICONS", "/PALETTES", "/MELODIES", "/SCRIPTS"})
    stdfs::create_directories(stdfs::u8path(sim::dataDir() + d), ec);
  return !ec;
}

}
}
