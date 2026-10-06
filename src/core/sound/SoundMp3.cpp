#include "core/sound/SoundMp3.h"

namespace awtrix {
namespace sound {

bool validName(const std::string& name) {
  if (name.empty() || name.size() > kMaxMp3Name) return false;
  for (char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

std::string mp3PathFor(const std::string& name) {
  if (!validName(name)) return "";
  return std::string(kDir) + name + kExt;
}

std::string melodyPathFor(const std::string& name) {
  if (!validName(name)) return "";
  return std::string(kMelodiesDir) + name + ".txt";
}

std::string scriptSoundDir(const std::string& script) {
  if (!validName(script)) return "";
  return std::string(kScriptsDir) + script;
}

std::string scriptMp3PathFor(const std::string& script, const std::string& name) {
  const std::string dir = scriptSoundDir(script);
  if (dir.empty() || !validName(name)) return "";
  return dir + "/" + name + kExt;
}

std::string mp3NameOfFile(const std::string& file) {
  constexpr size_t kSuffix = sizeof(".mp3") - 1;
  if (file.size() <= kSuffix || file.compare(file.size() - kSuffix, kSuffix, kExt) != 0) return "";
  std::string name = file.substr(0, file.size() - kSuffix);
  return validName(name) ? name : "";
}

std::string namesakePath(const std::string& path) {
  const std::string mp3s = kDir;
  const std::string melodies = kMelodiesDir;
  if (path.compare(0, mp3s.size(), mp3s) == 0)
    return melodyPathFor(mp3NameOfFile(path.substr(mp3s.size())));
  if (path.compare(0, melodies.size(), melodies) != 0) return "";
  const std::string file = path.substr(melodies.size());
  constexpr size_t kSuffix = sizeof(".txt") - 1;
  if (file.size() <= kSuffix || file.compare(file.size() - kSuffix, kSuffix, ".txt") != 0)
    return "";
  return mp3PathFor(file.substr(0, file.size() - kSuffix));
}

bool isScriptMp3Path(const std::string& path) {
  const std::string prefix = kScriptsDir;
  if (path.compare(0, prefix.size(), prefix) != 0) return false;
  const size_t slash = path.find('/', prefix.size());
  if (slash == std::string::npos) return false;
  const std::string name = mp3NameOfFile(path.substr(slash + 1));
  return !name.empty() &&
         scriptMp3PathFor(path.substr(prefix.size(), slash - prefix.size()), name) == path;
}

bool within(const std::string& path, const std::string& place) {
  if (place.empty() || path.compare(0, place.size(), place) != 0) return false;
  return path.size() == place.size() || place.back() == '/' || path[place.size()] == '/';
}

}
}
