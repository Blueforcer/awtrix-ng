#pragma once

#include <functional>
#include <string>
#include <utility>

#include "core/Command.h"
#include "core/Services.h"

namespace awtrix::script {

// Stands in for the real service when the script engine is switched off: uploads are still
// stored and removed, so nothing is lost, but no script is ever compiled or run.
class ScriptSourceService : public IScriptService {
 public:
  using SaveFn = std::function<void(const std::string& name, const std::string& source)>;
  using RemoveFn = std::function<void(const std::string& name)>;

  ScriptSourceService(SaveFn save, RemoveFn remove)
      : save_(std::move(save)), remove_(std::move(remove)) {}

  DispatchResult setScript(const std::string& name, const std::string& source,
                           DispatchDetail& detail) override {
    (void)detail;
    if (save_) save_(name, source);
    return DispatchResult::Ok;
  }

  void removeScript(const std::string& name) override {
    if (remove_) remove_(name);
  }

 private:
  SaveFn save_;
  RemoveFn remove_;
};

}
