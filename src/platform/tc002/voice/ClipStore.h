#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <random>
#include <string>

#include "platform/linux/LinuxMemory.h"
#include "platform/posix/Files.h"
#include "platform/tc002/audio/ClipKind.h"

namespace awtrix::tc002::voice {

using ClipId = uint64_t;

struct Clip {
  std::string app;
  std::string path;
  ClipKind kind = ClipKind::Wav;
  std::size_t bytes = 0;
  uint32_t ms = 0;
  bool ready = false;
};

class ClipStore {
 public:
  static constexpr std::size_t kClipsPerApp = 4, kBytesPerApp = 4 * 1024 * 1024;

  ClipStore(const std::string& directory, MemoryRoom memory);

  bool ready() const { return folder_.valid(); }
  ClipId reserve(const std::string& app, std::size_t bytes);
  std::string path(ClipId id) const;
  bool commit(ClipId id, std::size_t bytes, uint32_t ms, ClipKind kind);
  const Clip* find(const std::string& app, ClipId id) const;
  void release(const std::string& app, ClipId id);
  void releaseReady(const std::string& app, ClipId id);
  void releaseReady(const std::string& app);
  void forget(const std::string& app);

 private:
  ClipId unusedId();
  void erase(std::map<ClipId, Clip>::iterator it);

  posix::PrivateTemporaryDirectory folder_;
  MemoryRoom memory_;
  std::map<ClipId, Clip> clips_;
  std::random_device random_;
  std::uniform_int_distribution<ClipId> ids_{1, INT64_MAX};
};

}
