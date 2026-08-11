#include "persistence/ScriptStore.h"

#include <LittleFS.h>

#include <cstring>
#include <utility>
#include <vector>

#include "system/Log.h"

namespace awtrix {

namespace {

constexpr const char* kDir = "/SCRIPTS";
constexpr const char* kSourceExt = ".ax";
constexpr const char* kStoreExt = ".store.json";

bool nameIsSafe(const std::string& name) {
  if (name.empty() || name.size() > 64) return false;
  if (name.find('/') != std::string::npos) return false;
  if (name.find('\\') != std::string::npos) return false;
  return name.find("..") == std::string::npos;
}

String sourcePath(const std::string& name) {
  return String(kDir) + "/" + name.c_str() + kSourceExt;
}

String storePath(const std::string& name) {
  return String(kDir) + "/" + name.c_str() + kStoreExt;
}

bool writeFile(const String& path, const std::string& body) {
  File f = LittleFS.open(path, "w");
  if (!f) {
    logf("scripts: cannot write %s", path.c_str());
    return false;
  }
  const std::size_t n = f.write(reinterpret_cast<const uint8_t*>(body.data()), body.size());
  f.close();
  if (n != body.size()) {
    logf("scripts: short write on %s (%u/%u)", path.c_str(), static_cast<unsigned>(n),
         static_cast<unsigned>(body.size()));
    return false;
  }
  return true;
}

// usedBytes() walks the whole block allocation to add its answer up, so this is the reading
// FsFreeSpace exists to keep off the hot path.
std::size_t readFreeBytes() {
  const std::size_t total = LittleFS.totalBytes();
  const std::size_t used = LittleFS.usedBytes();
  return total > used ? total - used : 0;
}

std::string readFile(const String& path) {
  File f = LittleFS.open(path, "r");
  if (!f) return {};
  const std::size_t size = f.size();
  std::string out;
  if (size) {
    out.resize(size);
    const std::size_t n = f.read(reinterpret_cast<uint8_t*>(&out[0]), size);
    out.resize(n);
  }
  f.close();
  return out;
}

}

bool ScriptStore::fitsOnDisk(std::size_t bytes) {
  return fitsWithMargin(free_.bytes(readFreeBytes), bytes);
}

// What is already queued for the other scripts. Charged alongside the incoming write so a burst
// between two flushes is weighed as one landing, not as each write having the disk to itself.
std::size_t ScriptStore::pendingBytesExcept(const std::string& script) const {
  std::size_t n = 0;
  for (const auto& kv : dirty_)
    if (kv.first != script) n += kv.second.size();
  return n;
}

void ScriptStore::save(const std::string& name, const std::string& source) {
  if (!nameIsSafe(name)) return;
  if (!fitsOnDisk(source.size() + pendingBytesExcept())) {
    logf("scripts: %s not saved, no room on flash (%u bytes)", name.c_str(),
         static_cast<unsigned>(source.size()));
    return;
  }
  flush();
  writeFile(sourcePath(name), source);
  free_.stale();
}

void ScriptStore::remove(const std::string& name) {
  if (!nameIsSafe(name)) return;
  // Drop the pending write first: the flush at the end would otherwise recreate the store file
  // that is about to be deleted.
  dirty_.erase(name);
  const String src = sourcePath(name);
  const String st = storePath(name);
  if (LittleFS.exists(src)) LittleFS.remove(src);
  if (LittleFS.exists(st)) LittleFS.remove(st);
  flush();
  free_.stale();
}

void ScriptStore::storeChanged(const std::string& script, const std::string& json) {
  if (!nameIsSafe(script)) return;
  if (!fitsOnDisk(json.size() + pendingBytesExcept(script))) {
    if (!storeRefused_) {
      storeRefused_ = true;
      logf("scripts: store not saved for %s, no room on flash (%u bytes)", script.c_str(),
           static_cast<unsigned>(json.size()));
    }
    return;
  }
  storeRefused_ = false;
  dirty_[script] = json;
}

void ScriptStore::tick(int64_t nowMs) {
  // Everything else on the device writes to the same filesystem, so the remembered free-space
  // figure is let expire on the flush interval as well. Nothing is read here: the next store
  // write pays for the reading, and a device where no script writes never pays at all.
  if (nowMs - lastFreeMs_ >= kFlushIntervalMs || nowMs < lastFreeMs_) {
    free_.stale();
    lastFreeMs_ = nowMs;
  }
  if (dirty_.empty()) {
    lastFlushMs_ = nowMs;
    return;
  }
  if (nowMs - lastFlushMs_ < kFlushIntervalMs) return;
  flush();
  lastFlushMs_ = nowMs;
}

void ScriptStore::flush() {
  if (dirty_.empty()) return;
  const std::map<std::string, std::string> pending = std::move(dirty_);
  dirty_.clear();
  for (const auto& kv : pending) writeFile(storePath(kv.first), kv.second);
  free_.stale();
}

bool ScriptStore::readSource(const std::string& name, std::string& out) const {
  if (!nameIsSafe(name)) return false;
  out = readFile(sourcePath(name));
  return !out.empty();
}

bool ScriptStore::readStore(const std::string& name, std::string& out) const {
  if (!nameIsSafe(name)) return false;
  // Unflushed writes must be visible immediately, so the pending map wins over the file.
  auto it = dirty_.find(name);
  if (it != dirty_.end()) {
    out = it->second;
    return !out.empty();
  }
  out = readFile(storePath(name));
  return !out.empty();
}

std::vector<std::string> ScriptStore::names() const {
  std::vector<std::string> names;
  File dir = LittleFS.open(kDir);
  if (!dir || !dir.isDirectory()) return names;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String fn = f.name();
    f.close();
    const int slash = fn.lastIndexOf('/');
    const String base = slash >= 0 ? fn.substring(slash + 1) : fn;
    if (!base.endsWith(kSourceExt)) continue;
    const String stem = base.substring(0, base.length() - strlen(kSourceExt));
    if (stem.length() == 0) continue;
    if (!nameIsSafe(stem.c_str())) continue;
    names.emplace_back(stem.c_str());
  }
  dir.close();
  return names;
}

void ScriptStore::loadAll(const LoadFn& cb) {
  if (!cb) return;
  for (const auto& name : names()) {
    const std::string source = readFile(sourcePath(name));
    if (source.empty()) {
      logf("scripts: skipped empty %s%s", name.c_str(), kSourceExt);
      continue;
    }
    cb(name, source, readFile(storePath(name)));
  }
}

}
