#include "persistence/ScriptFiles.h"

#include <LittleFS.h>

#include "persistence/Filesystem.h"
#include "persistence/FsFreeSpace.h"
#include "system/Log.h"

namespace awtrix::scriptfiles {

bool LittleFsFiles::write(const std::string& path, const std::string& body) {
  File file = LittleFS.open(path.c_str(), "w");
  if (!file) { logf("scripts: cannot write %s", path.c_str()); return false; }
  const auto count = file.write(reinterpret_cast<const uint8_t*>(body.data()), body.size());
  file.close();
  if (count == body.size()) return true;
  logf("scripts: short write on %s (%u/%u)", path.c_str(), static_cast<unsigned>(count),
       static_cast<unsigned>(body.size()));
  return false;
}

bool LittleFsFiles::read(const std::string& path, std::string& out) {
  File file = LittleFS.open(path.c_str(), "r");
  if (!file) return false;
  std::string bytes(file.size(), '\0');
  if (!bytes.empty() && file.read(reinterpret_cast<uint8_t*>(&bytes[0]), bytes.size()) != bytes.size())
    return false;
  out = std::move(bytes);
  return true;
}

void LittleFsFiles::remove(const std::string& path) { LittleFS.remove(path.c_str()); }

std::size_t LittleFsFiles::freeBytes() {
  std::size_t total = 0, used = 0;
  return fs::usage(total, used) && total > used ? total - used : 0;
}

bool LittleFsFiles::acceptSource(const std::string& name, const std::string& source, std::size_t pending) {
  if (fitsWithMargin(freeBytes(), source.size() + pending)) return true;
  logf("scripts: %s not saved, no room on flash (%u bytes)", name.c_str(), static_cast<unsigned>(source.size()));
  return false;
}

void LittleFsFiles::saveSource(const std::string& name, const std::string& source) {
  const auto target = sourcePath(name), temporary = target + ".tmp";
  std::string check;
  if (write(temporary, source) && read(temporary, check) && check == source &&
      !LittleFS.rename(temporary.c_str(), target.c_str()))
    logf("scripts: cannot replace %s", target.c_str());
  LittleFS.remove(temporary.c_str());
}

void LittleFsFiles::forEachName(const NameVisitor& visit) {
  File dir = LittleFS.open("/SCRIPTS");
  if (!dir || !dir.isDirectory()) return;
  for (File file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) continue;
    std::string_view leaf = file.name();
    const auto slash = leaf.rfind('/');
    if (slash != std::string_view::npos) leaf.remove_prefix(slash + 1);
    if (visit(leaf)) break;
  }
}

}
