#include "persistence/LittleFsRestoreSink.h"

#include "core/AssetPaths.h"

namespace awtrix::backup {
bool LittleFsRestoreSink::beginFile(const std::string& path, std::string& err) {
  abortFile();
  // RestoreApplier checks entries against the shared backup policy; the sink applies the same one
  // so a direct caller cannot write outside the asset directories either.
  const auto slash = path.find_last_of('/');
  if (!assets::isBackupWritable(path) || slash + 1 == path.size() ||
      path.find("//") != std::string::npos || path.find('\\') != std::string::npos ||
      path.find('\0') != std::string::npos) {
    err = "invalid asset path"; return false;
  }
  // A nested entry such as /ICONS/sub/smile.gif needs every parent, and mkdir makes one level.
  for (auto at = path.find('/', 1); at <= slash; at = path.find('/', at + 1)) {
    const std::string dir = path.substr(0, at);
    if (!LittleFS.exists(dir.c_str()) && !LittleFS.mkdir(dir.c_str())) {
      err = "could not create directory"; return false;
    }
  }
  path_ = path; temp_ = path + ".restore.tmp";
  file_ = LittleFS.open(temp_.c_str(), "w");
  failed_ = !file_;
  if (failed_) err = "could not open for writing";
  return !failed_;
}
bool LittleFsRestoreSink::writeFile(const uint8_t* data, std::size_t n) {
  if (!file_ || failed_) return false;
  failed_ = file_.write(data, n) != n;
  return !failed_;
}
bool LittleFsRestoreSink::endFile() {
  if (!file_ || failed_) { abortFile(); return false; }
  file_.flush(); file_.close();
  const bool ok = LittleFS.rename(temp_.c_str(), path_.c_str());
  if (!ok) { abortFile(); return false; }
  path_.clear(); temp_.clear(); return true;
}
void LittleFsRestoreSink::abortFile() {
  if (file_) file_.close();
  if (!temp_.empty()) LittleFS.remove(temp_.c_str());
  path_.clear(); temp_.clear(); failed_ = false;
}
}
