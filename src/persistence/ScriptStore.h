#pragma once

#include <cstdint>

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/script/ScriptServices.h"
#include "persistence/FsFreeSpace.h"

namespace awtrix {

// Owns everything a script keeps on flash: the source as /SCRIPTS/<name>.ax and its persistent
// key/value store as /SCRIPTS/<name>.store.json.
class ScriptStore : public script::IScriptStoreSink {
 public:
  using LoadFn =
      std::function<void(const std::string& name, const std::string& source,
                         const std::string& storeJson)>;

  void save(const std::string& name, const std::string& source);

  void remove(const std::string& name);

  void loadAll(const LoadFn& cb);

  std::vector<std::string> names() const;

  bool readSource(const std::string& name, std::string& out) const;

  bool readStore(const std::string& name, std::string& out) const;

  void storeChanged(const std::string& script, const std::string& json) override;

  void tick(int64_t nowMs);

  void flush();

  std::size_t pendingCount() const { return dirty_.size(); }

 private:
  // A script may write its store on every loop, so writes are held here and land on flash at
  // most this often — each one is an erase cycle out of a finite budget.
  static constexpr long kFlushIntervalMs = 5000;

  bool fitsOnDisk(std::size_t bytes);
  std::size_t pendingBytesExcept(const std::string& script = std::string()) const;

  std::map<std::string, std::string> dirty_;
  int64_t lastFlushMs_ = 0;
  int64_t lastFreeMs_ = 0;
  FsFreeSpace free_;
  // A refused store write repeats for as long as the script keeps writing, so it is reported on
  // the way in rather than every time.
  bool storeRefused_ = false;
};

}
