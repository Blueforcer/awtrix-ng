#pragma once

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

#include "persistence/Filesystem.h"
#include "persistence/ScriptStore.h"
#include "platform/linux/host/HostStore.h"

namespace awtrix::host {

class ScriptFiles {
 public:
  static constexpr bool keepPendingWhenFull = true;

  static bool read(const std::string& path, std::string& out) { return readFile(hostPath(path), out); }
  static bool write(const std::string& path, const std::string& body) {
    if (writeFile(hostPath(path), body)) return true;
    logf("scripts: cannot write %s", path.c_str());
    return false;
  }
  static void remove(const std::string& path) {
    std::error_code error;
    std::filesystem::remove(std::filesystem::u8path(hostPath(path)), error);
  }
  static void forEachName(const scriptfiles::NameVisitor& visit) {
    namespace stdfs = std::filesystem;
    const auto directory = hostPath("/SCRIPTS");
    if (directory.empty()) return;
    std::error_code error;
    for (stdfs::directory_iterator it(stdfs::u8path(directory), error), end;
         !error && it != end; it.increment(error)) {
      const auto status = it->symlink_status(error);
      if (!error && stdfs::is_regular_file(status) && visit(it->path().filename().u8string())) break;
    }
  }

  bool readSource(const std::string& name, std::string& out) const {
    const auto pending = sources_.find(name);
    if (pending == sources_.end()) return read(scriptfiles::sourcePath(name), out);
    out = pending->second;
    return true;
  }

  std::size_t pendingBytes(const std::string& except = {}) const {
    std::size_t bytes = 0;
    for (const auto& source : sources_) if (source.first != except) bytes += source.second.size();
    return bytes;
  }
  bool hasPending() const { return !sources_.empty() || !refusedStores_.empty() || !refusedSources_.empty(); }
  void addPendingNames(std::vector<std::string>& names) const {
    for (const auto& source : sources_)
      if (std::find(names.begin(), names.end(), source.first) == names.end()) names.push_back(source.first);
  }

  bool acceptSource(const std::string& name, const std::string& source, std::size_t pending) {
    const auto previous = sources_.find(name);
    const auto otherBytes = pending - (previous == sources_.end() ? 0 : previous->second.size());
    if (!retain(name, source.size(), otherBytes, refusedSources_)) return false;
    sources_.insert_or_assign(name, std::string(source));
    if (sourceFits(source.size() + otherBytes)) return true;
    logf("scripts: %s remains pending, no room on flash (%u bytes)", name.c_str(),
         static_cast<unsigned>(source.size()));
    return false;
  }
  static void saveSource(const std::string&, const std::string&) {}
  void flushSources() {
    for (auto it = sources_.begin(); it != sources_.end();) {
      if (sourceFits(it->second.size()) && writeUpload(hostPath(scriptfiles::sourcePath(it->first)), it->second))
        it = sources_.erase(it);
      else ++it;
    }
  }

  bool allowStore(const std::string& name, const std::string& body, std::size_t otherBytes) {
    if (uploadReserve() && body.size() > kReservedStoreLimitBytes) {
      if (oversizedStores_.insert(name).second)
        logf("scripts: store not saved for %s, %u bytes over the %u byte limit", name.c_str(),
             static_cast<unsigned>(body.size()), static_cast<unsigned>(kReservedStoreLimitBytes));
      return false;
    }
    oversizedStores_.erase(name);
    return retain(name, body.size(), otherBytes, refusedStores_);
  }
  bool waitsForRoom(const std::string& name, std::size_t bytes, std::size_t free) const {
    const auto failed = failedStores_.find(name);
    return uploadReserve() && failed != failedStores_.end() &&
           bytes >= failed->second.bytes && free <= failed->second.freeBytes;
  }
  void storeWritten(const std::string& name, bool ok, std::size_t bytes, std::size_t free) {
    if (ok) failedStores_.erase(name);
    else if (uploadReserve()) failedStores_[name] = {bytes, free};
  }
  void forget(const std::string& name) {
    sources_.erase(name);
    refusedSources_.erase(name);
    refusedStores_.erase(name);
    oversizedStores_.erase(name);
    failedStores_.erase(name);
  }
  static std::size_t freeBytes() {
    return static_cast<std::size_t>(std::min<uint64_t>(quotaFreeBytes(), stateRoom()));
  }

 private:
  static constexpr std::size_t kReservedStoreLimitBytes = 64 * 1024;
  struct FailedWrite { std::size_t bytes, freeBytes; };

  static std::size_t quotaFreeBytes() {
    std::size_t total = 0, used = 0;
    return fs::usage(total, used) && total > used ? total - used : 0;
  }
  static bool sourceFits(std::size_t bytes) {
    const uint64_t used = kFsTotalBytes - quotaFreeBytes(), capacity = storageCapacity(used);
    return fitsWithMargin(capacity > used ? static_cast<std::size_t>(capacity - used) : 0, bytes);
  }
  // Refused payloads remain observable until replaced or removed, even after other writes flush.
  static bool retain(const std::string& name, std::size_t bytes, std::size_t otherBytes,
                     std::set<std::string>& refused) {
    if (bytes > kFsTotalBytes || otherBytes > kFsTotalBytes - bytes) {
      if (refused.insert(name).second)
        logf("scripts: unresolved write for %s, pending payload limit exceeded", name.c_str());
      return false;
    }
    refused.erase(name);
    return true;
  }

  std::map<std::string, std::string> sources_;
  std::set<std::string> refusedStores_, refusedSources_, oversizedStores_;
  std::map<std::string, FailedWrite> failedStores_;
};

}
