#pragma once

#include <functional>
#include <string>
#include <utility>

#include "core/Command.h"
#include "core/Services.h"
#include "core/script/ScriptConfig.h"
#include "core/script/ScriptHost.h"

namespace awtrix::script {

class ScriptService : public IScriptService {
 public:
  using SaveFn = std::function<void(const std::string& name, const std::string& source)>;
  using RemoveFn = std::function<void(const std::string& name)>;

  ScriptService(ScriptHost& host, SaveFn save, RemoveFn remove)
      : host_(host), save_(std::move(save)), remove_(std::move(remove)) {}

  DispatchResult setScript(const std::string& name, const std::string& source,
                           DispatchDetail& detail) override {
    std::string storeJson;
    readStore(name, storeJson);
    dropSettingsTheSourceNoLongerDeclares(name, source, storeJson);

    const DispatchResult r = install(name, source, storeJson, "script install refused", detail);
    if (r == DispatchResult::Ok && save_) save_(name, source);
    return r;
  }

  DispatchResult setScriptConfig(const std::string& name, const std::string& json,
                                 DispatchDetail& detail) override {
    std::string source;
    if (!readSource(name, source)) {
      detail.field = "name";
      detail.message = "no such script";
      return DispatchResult::NotFound;
    }
    std::string storeJson;
    readStore(name, storeJson);

    const ConfigPatch patch = applyConfigPatch(parseConfig(source), storeJson, json);
    if (!patch.ok) {
      detail.field = patch.field;
      detail.message = patch.message;
      return DispatchResult::ValidationError;
    }
    if (patch.storeJson.size() > kMaxStoreBytes) {
      detail.field = "name";
      detail.message = "this script has no room left to store the change; shorten a text setting";
      return DispatchResult::Capacity;
    }

    const DispatchResult r = install(name, source, patch.storeJson, "settings not applied", detail);
    if (r == DispatchResult::Ok) saveStore(name, patch.storeJson);
    return r;
  }

  void removeScript(const std::string& name) override {
    host_.remove(name);
    if (remove_) remove_(name);
  }

  bool scriptWantsShow(const std::string& name) override { return host_.wantsShow(name); }

  long scriptDurationMs(const std::string& name) override { return host_.durationMs(name); }
  bool scriptScrollHolds(const std::string& name) override { return host_.scrollHolds(name); }
  bool scriptIsHeadless(const std::string& name) override { return host_.isHeadless(name); }

  void setRunningScripts(const std::vector<std::string>& running) override {
    host_.setRunningScripts(running);
  }

 private:
  bool readSource(const std::string& name, std::string& out) const {
    const ScriptServices& svc = host_.services();
    return svc.readSource && svc.readSource(name, out);
  }

  bool readStore(const std::string& name, std::string& out) const {
    const ScriptServices& svc = host_.services();
    return svc.readStore && svc.readStore(name, out);
  }

  void saveStore(const std::string& name, const std::string& json) const {
    const ScriptServices& svc = host_.services();
    if (svc.storeSink) svc.storeSink->storeChanged(name, json);
  }

  // Compares the source about to be replaced against the incoming one, so this has to run
  // before the install writes the new source over the old.
  void dropSettingsTheSourceNoLongerDeclares(const std::string& name, const std::string& source,
                                             std::string& storeJson) const {
    if (storeJson.empty()) return;
    std::string pruned;
    {
      std::string previous;
      if (!readSource(name, previous)) return;
      if (!dropUndeclaredValues(parseConfig(previous), parseConfig(source), storeJson, pruned))
        return;
    }
    storeJson = std::move(pruned);
    saveStore(name, storeJson);
  }

  // Ok means installed, not working: a script that compiles and then throws returns Ok with
  // the error in `detail`, because the upload did succeed and the author must see the message.
  DispatchResult install(const std::string& name, const std::string& source,
                         const std::string& storeJson, const char* refusal,
                         DispatchDetail& detail) {
    if (!host_.set(name, source, storeJson)) {
      detail.field = "name";
      detail.message = host_.lastRefusal();
      if (detail.message.empty()) detail.message = refusal;
      if (host_.refusalIsInvalid()) return DispatchResult::ValidationError;
      return host_.refusalIsTransient() ? DispatchResult::Busy : DispatchResult::Capacity;
    }
    const ScriptError err = host_.errorOf(name);
    detail.message = err.message;
    detail.line = err.line;
    detail.hook = err.hook;
    return DispatchResult::Ok;
  }

  ScriptHost& host_;
  SaveFn save_;
  RemoveFn remove_;
};

}
