#include "platform/tc002/voice/ClipStore.h"

#include <unistd.h>

#include <iterator>
#include <utility>

namespace awtrix::tc002::voice {

constexpr char kFolderPrefix[] = "awtrix-clips";

void ClipStore::removeLeftovers(const std::string& directory) {
  posix::removeStaleTemporaryDirectories(directory, kFolderPrefix);
}

ClipStore::ClipStore(const std::string& directory, MemoryRoom memory)
    : folder_(directory, kFolderPrefix), memory_(std::move(memory)) {}

ClipId ClipStore::reserve(const std::string& app, std::size_t bytes) {
  if (!folder_.valid()) return 0;
  std::size_t count = 0, used = 0;
  for (const auto& entry : clips_) {
    if (entry.second.app != app) continue;
    ++count;
    used += entry.second.bytes;
  }
  if (count >= kClipsPerApp || bytes > kBytesPerApp - used || !fits(bytes)) return 0;
  const ClipId id = unusedId();
  Clip& clip = clips_[id];
  clip.app = app;
  clip.path = folder_.path() + "/" + std::to_string(id);
  clip.bytes = bytes;
  return id;
}

ClipId ClipStore::promise(const std::string& app, std::size_t bytes) {
  const ClipId id = reserve(app, bytes);
  if (id) {
    clips_[id].promised = bytes;
    promised_ += bytes;
  }
  return id;
}

void ClipStore::keepPromise(Clip& clip) {
  promised_ -= clip.promised;
  clip.promised = 0;
}

ClipId ClipStore::unusedId() {
  for (;;) {
    const ClipId id = ids_(random_);
    if (!clips_.count(id)) return id;
  }
}

std::string ClipStore::path(ClipId id) const {
  const auto it = clips_.find(id);
  return it == clips_.end() ? std::string() : it->second.path;
}

bool ClipStore::commit(ClipId id, std::size_t bytes, ClipKind kind) {
  const auto it = clips_.find(id);
  if (it == clips_.end() || it->second.ready) return false;
  if (bytes > it->second.bytes) {
    erase(it);
    return false;
  }
  keepPromise(it->second);
  it->second.bytes = bytes;
  it->second.kind = kind;
  it->second.ready = true;
  return true;
}

const Clip* ClipStore::find(const std::string& app, ClipId id) const {
  const auto it = clips_.find(id);
  return it != clips_.end() && it->second.ready && it->second.app == app ? &it->second : nullptr;
}

void ClipStore::release(const std::string& app, ClipId id) {
  const auto it = clips_.find(id);
  if (it != clips_.end() && it->second.app == app) erase(it);
}

void ClipStore::releaseReady(const std::string& app, ClipId id) {
  if (find(app, id)) erase(clips_.find(id));
}

template <class Gone>
void ClipStore::eraseIf(Gone gone) {
  for (auto it = clips_.begin(); it != clips_.end();) {
    const auto next = std::next(it);
    if (gone(it->second)) erase(it);
    it = next;
  }
}

void ClipStore::releaseReady(const std::string& app) {
  eraseIf([&app](const Clip& clip) { return clip.app == app && clip.ready; });
}

void ClipStore::forget(const std::string& app) {
  eraseIf([&app](const Clip& clip) { return clip.app == app; });
}

void ClipStore::erase(std::map<ClipId, Clip>::iterator it) {
  keepPromise(it->second);
  ::unlink(it->second.path.c_str());
  clips_.erase(it);
}

}
