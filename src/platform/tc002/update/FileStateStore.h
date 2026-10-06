#pragma once

#include <functional>
#include <string>

#include "platform/tc002/update/StateStore.h"

namespace awtrix::tc002::update {

// update-state.json inside a caller-owned 0700 directory. The directory
// descriptor stays open and flock'ed for the object's lifetime, which
// serializes processes on one host; a forked child inherits the lock and must
// not use the store. The fault hook is consulted before each syscall step and
// makes that step fail when it returns true.
class FileStateStore final : public StateStore {
 public:
  using Fault = std::function<bool(const char* step)>;
  explicit FileStateStore(const std::string& directory, Fault fault = {});
  ~FileStateStore() override;
  FileStateStore(const FileStateStore&) = delete;
  FileStateStore& operator=(const FileStateStore&) = delete;
  bool ok() const { return directory_ >= 0; }
  const std::string& error() const { return error_; }
  bool read(std::string& json, bool& exists, std::string& error) override;
  bool write(const std::string& json, std::string& error) override;

 private:
  bool faulted(const char* step) const { return fault_ && fault_(step); }
  int directory_ = -1;
  Fault fault_;
  std::string error_;
};


}  // namespace awtrix::tc002::update
