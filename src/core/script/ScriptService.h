#pragma once

#include <functional>
#include <string>
#include <utility>

#include "core/Command.h"
#include "core/Services.h"
#include "core/api/JsonReader.h"
#include "core/script/ScriptConfig.h"
#include "core/script/ScriptData.h"
#include "core/script/ScriptHeap.h"
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

  // Runs on the command loop: checking the previous source and installing are serialized
  // with other edits. Keep the old source on disk until the replacement has loaded.
  DispatchResult updateScript(const std::string& name, const std::string& json,
                              DispatchDetail& detail) override {
    api::JsonReader expectedValue, sourceValue;
    if (!api::readMembers(json, {{"expected_source", &expectedValue}, {"source", &sourceValue}}))
      return DispatchResult::ParseError;
    std::string expected, source;
    const bool createOnly = expectedValue.isNull();
    if ((!createOnly && (!expectedValue.isString() || !expectedValue.appendString(expected))) ||
        !sourceValue.isString() || !sourceValue.appendString(source) || source.empty()) {
      detail.message = "invalid source or expected_source";
      return DispatchResult::ValidationError;
    }
    std::string previous;
    const bool exists = readSource(name, previous);
    if (createOnly) {
      if (exists) return DispatchResult::Conflict;
      if (!save_ || !remove_) return DispatchResult::Unavailable;
      const DispatchResult result = install(name, source, "{}", "install refused", detail);
      if (result != DispatchResult::Ok) return result;
      if (detail.message.empty()) {
        save_(name, source);
        std::string saved;
        if (readSource(name, saved) && saved == source) return DispatchResult::Ok;
        detail.message = "source not saved";
      }
      removeScript(name);
      return DispatchResult::ValidationError;
    }
    if (!exists) return DispatchResult::NotFound;
    if (previous != expected) return DispatchResult::Conflict;
    if (previous == source) return DispatchResult::Ok;
    if (!save_) return DispatchResult::Unavailable;
    std::string oldStore, nextStore, pruned;
    readStore(name, oldStore);
    nextStore = oldStore;
    if (dropUndeclaredValues(parseConfig(previous), parseConfig(source), oldStore, pruned))
      nextStore = std::move(pruned);
    const DispatchResult result = install(name, source, nextStore, "update refused", detail);
    if (result != DispatchResult::Ok) return result;
    if (detail.message.empty()) {
      save_(name, source);
      std::string saved;
      if (readSource(name, saved) && saved == source) {
        std::string stored, cleaned;
        if (readStore(name, stored) &&
            dropUndeclaredValues(parseConfig(previous), parseConfig(source), stored, cleaned))
          saveStore(name, cleaned);
        return DispatchResult::Ok;
      }
      detail.message = "source not saved";
    }
    const std::string failure = detail.message;
    const bool restored = host_.set(name, previous, oldStore) && host_.errorOf(name).message.empty();
    saveStore(name, oldStore.empty() ? "{}" : oldStore);
    detail.message = failure + (restored ? "; old version restored" : "; old version kept, restart");
    return DispatchResult::ValidationError;
  }

  DispatchResult setScriptConfig(const std::string& name, const std::string& json,
                                 DispatchDetail& detail) override {
    return patchStore(name, json, applyConfigPatch, "settings not applied",
                      "not enough memory",
                      detail);
  }

  // The script keeps its store in memory, so a change only reaches it by a restart.
  DispatchResult setScriptData(const std::string& name, const std::string& json,
                               DispatchDetail& detail) override {
    return patchStore(name, json, applyDataPatch, "data not applied",
                      "not enough memory", detail);
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

  bool scriptIsOnDemand(const std::string& name) override { return host_.isOnDemand(name); }

  // Like an install, Ok means started, not working: a script that throws in setup() is running
  // and shows its error.
  DispatchResult launchScript(const std::string& name, DispatchDetail& detail) override {
    if (!host_.launch(name)) {
      const DispatchResult refusal = refused("script start refused", detail);
      return host_.refusalIsInvalid() ? DispatchResult::NotFound : refusal;
    }
    reportError(name, detail);
    return DispatchResult::Ok;
  }

  void unloadScript(const std::string& name) override { host_.unload(name); }

  std::string scriptTitle(const std::string& name) override {
    const ScriptMeta* meta = host_.metaOf(name);
    return meta && !meta->name.empty() ? meta->name : name;
  }

 private:
  using StorePatchFn = StorePatch (*)(const ConfigSchema&, const std::string&,
                                      const std::string&);

  DispatchResult patchStore(const std::string& name, const std::string& json,
                            StorePatchFn apply, const char* refusal, const char* tooBig,
                            DispatchDetail& detail) {
    std::string source;
    if (!readSource(name, source)) {
      detail.field = "name";
      detail.message = "no such script";
      return DispatchResult::NotFound;
    }
    std::string storeJson;
    readStore(name, storeJson);

    const StorePatch patch = apply(parseConfig(source), storeJson, json);
    if (patch.malformed) return DispatchResult::ParseError;
    if (!patch.ok) {
      detail.field = patch.field;
      detail.message = patch.message;
      return DispatchResult::ValidationError;
    }
    if (patch.storeJson.size() > heap::growthBudget()) {
      detail.field = "name";
      detail.message = tooBig;
      return DispatchResult::Capacity;
    }

    // Saved before the restart, so what the app itself stores while starting comes last.
    saveStore(name, patch.storeJson);
    const DispatchResult r = install(name, source, patch.storeJson, refusal, detail);
    if (r != DispatchResult::Ok) saveStore(name, storeJson.empty() ? "{}" : storeJson);
    return r;
  }

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
    if (!host_.set(name, source, storeJson)) return refused(refusal, detail);
    reportError(name, detail);
    return DispatchResult::Ok;
  }

  DispatchResult refused(const char* fallback, DispatchDetail& detail) const {
    detail.field = "name";
    detail.message = host_.lastRefusal();
    if (detail.message.empty()) detail.message = fallback;
    if (host_.refusalIsInvalid()) return DispatchResult::ValidationError;
    return host_.refusalIsTransient() ? DispatchResult::Busy : DispatchResult::Capacity;
  }

  void reportError(const std::string& name, DispatchDetail& detail) const {
    const ScriptError err = host_.errorOf(name);
    detail.message = err.message;
    detail.line = err.line;
    detail.hook = err.hook;
  }

  ScriptHost& host_;
  SaveFn save_;
  RemoveFn remove_;
};

}
