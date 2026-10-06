#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/script/IScriptFiles.h"
#include "core/script/ScriptMeta.h"
#include "persistence/FsFreeSpace.h"
#include "persistence/ScriptFiles.h"
#include "system/Log.h"

namespace awtrix {

// Sources and state share a name; sound files remain available after removing the script.
template <class Files = scriptfiles::LittleFsFiles>
class ScriptStore : public script::IScriptFiles, private Files {
 public:
  void save(const std::string& name, const std::string& source) override {
    if (!nameIsSafe(name) || !files().acceptSource(name, source, pendingBytesExcept())) return;
    flush();
    files().saveSource(name, source);
    free_.stale();
  }

  void remove(const std::string& name) override {
    if (!nameIsSafe(name)) return;
    dirty_.erase(name);
    files().forget(name);
    Files::remove(scriptfiles::sourcePath(name));
    Files::remove(scriptfiles::storePath(name));
    flush();
    free_.stale();
  }

  bool readSource(const std::string& name, std::string& out) const override {
    return nameIsSafe(name) && files().readSource(name, out) && !out.empty();
  }

  bool readStore(const std::string& name, std::string& out) const override {
    if (!nameIsSafe(name)) return false;
    const auto pending = dirty_.find(name);
    if (pending != dirty_.end()) out = pending->second;
    else if (!Files::read(scriptfiles::storePath(name), out)) return false;
    return !out.empty();
  }

  static bool readSavedSource(const std::string& name, std::string& out) {
    return nameIsSafe(name) && Files::read(scriptfiles::sourcePath(name), out) && !out.empty();
  }

  static std::vector<std::string> savedNames() {
    std::vector<std::string> out;
    Files::forEachName({&out, [](void* context, std::string_view leaf) {
      if (leaf.size() <= 3 || leaf.compare(leaf.size() - 3, 3, ".ax") != 0) return false;
      auto name = leaf.substr(0, leaf.size() - 3);
      if (scriptfiles::nameIsSafe(name)) static_cast<std::vector<std::string>*>(context)->emplace_back(name);
      return false;
    }});
    return out;
  }

  std::vector<std::string> names() const override {
    auto out = savedNames();
    files().addPendingNames(out);
    return out;
  }

  void loadAll(const LoadFn& cb) override {
    if (!cb) return;
    for (const auto& name : names()) {
      std::string source, store;
      if (!readSource(name, source)) {
        logf("scripts: skipped empty %s.ax", name.c_str());
        continue;
      }
      readStore(name, store);
      cb(name, source, store);
    }
  }

  std::vector<script::StoredScript> storedScripts() const {
    std::vector<script::StoredScript> out;
    for (const auto& name : names()) {
      std::string source;
      if (readSource(name, source)) out.push_back({name, script::parseMeta(source)});
    }
    return out;
  }

  void storeChanged(const std::string& name, const std::string& json) override {
    if (!nameIsSafe(name)) return;
    const auto otherBytes = pendingBytesExcept(name);
    if (!files().allowStore(name, json, otherBytes)) return;
    const bool fits = fitsOnDisk(json.size() + otherBytes);
    if (fits || Files::keepPendingWhenFull) dirty_.insert_or_assign(name, std::string(json));
    if (!fits && !storeRefused_)
      logf("scripts: store %s for %s, no room on flash (%u bytes)",
           Files::keepPendingWhenFull ? "remains pending" : "not saved", name.c_str(),
           static_cast<unsigned>(json.size()));
    storeRefused_ = !fits;
  }

  void tick(int64_t nowMs) {
    if (nowMs - lastFreeMs_ >= kFlushIntervalMs || nowMs < lastFreeMs_) {
      free_.stale();
      lastFreeMs_ = nowMs;
    }
    if (!hasPending()) { lastFlushMs_ = nowMs; return; }
    if (nowMs - lastFlushMs_ < kFlushIntervalMs) return;
    flush();
    lastFlushMs_ = nowMs;
  }

  void flush() {
    if (!hasPending()) return;
    free_.stale();
    files().flushSources();
    for (auto it = dirty_.begin(); it != dirty_.end();) {
      const auto bytes = it->second.size();
      if (!fitsOnDisk(bytes) || files().waitsForRoom(it->first, bytes, free_.bytes(Files::freeBytes))) {
        ++it;
        continue;
      }
      const bool ok = Files::write(scriptfiles::storePath(it->first), it->second);
      free_.stale();
      files().storeWritten(it->first, ok, bytes, free_.bytes(Files::freeBytes));
      if (ok) it = dirty_.erase(it);
      else ++it;
    }
    free_.stale();
  }

  bool hasPending() const { return !dirty_.empty() || files().hasPending(); }
  static bool nameIsSafe(const std::string& name) { return scriptfiles::nameIsSafe(name); }

 private:
  static constexpr int64_t kFlushIntervalMs = 5000;
  bool fitsOnDisk(std::size_t bytes) { return fitsWithMargin(free_.bytes(Files::freeBytes), bytes); }
  std::size_t pendingBytesExcept(const std::string& except = {}) const {
    std::size_t bytes = files().pendingBytes();
    for (const auto& item : dirty_) if (item.first != except) bytes += item.second.size();
    return bytes;
  }

  Files& files() { return *this; }
  const Files& files() const { return *this; }
  std::map<std::string, std::string> dirty_;
  int64_t lastFlushMs_ = 0, lastFreeMs_ = 0;
  FsFreeSpace free_;
  bool storeRefused_ = false;
};

}
