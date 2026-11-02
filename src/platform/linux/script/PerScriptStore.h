#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace awtrix::script {

using SourceReader = std::function<std::optional<std::string>(const std::string& app)>;

class PerScriptStore {
 public:
  virtual ~PerScriptStore() = default;
  virtual void dropStale() = 0;
  virtual void sourceSaved(const std::string& app) = 0;
  virtual void sourceRemoved(const std::string& app) = 0;
  virtual bool eraseAll() = 0;
};

class PerScriptStores {
 public:
  void add(PerScriptStore& store) { stores_.push_back(&store); }
  void dropStale() {
    for (PerScriptStore* store : stores_) store->dropStale();
  }
  void sourceSaved(const std::string& app) {
    for (PerScriptStore* store : stores_) store->sourceSaved(app);
  }
  void sourceRemoved(const std::string& app) {
    for (PerScriptStore* store : stores_) store->sourceRemoved(app);
  }
  bool eraseAll() {
    bool erased = true;
    for (PerScriptStore* store : stores_) erased = store->eraseAll() && erased;
    return erased;
  }

 private:
  std::vector<PerScriptStore*> stores_;
};

}
