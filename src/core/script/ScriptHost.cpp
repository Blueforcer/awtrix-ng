#include "core/script/ScriptHost.h"

#include <algorithm>
#include <cctype>

#include "core/apps/AppRegistry.h"
#include "core/apps/IApp.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptConfig.h"
#include "core/script/ScriptHeap.h"

namespace awtrix::script {
namespace {

bool isIdentifier(const std::string& s) {
  if (s.empty()) return false;
  const unsigned char first = static_cast<unsigned char>(s[0]);
  if (!std::isalpha(first) && first != '_') return false;
  for (char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (!std::isalnum(u) && u != '_') return false;
  }
  return true;
}

bool isReservedModule(const std::string& s) {
  static const char* const kReserved[] = {"json",  "math",  "string", "global",
                                          "gc",    "strict", "os",    "sys",
                                          "time",  "debug", "introspect", "solidify"};
  for (const char* r : kReserved)
    if (s == r) return true;
  return false;
}

// Scans for `import <name>` without parsing, so a name in a string or comment counts too.
// Over-collecting costs a needless reload; missing one leaves an importer on a stale module.
std::vector<std::string> collectImports(const std::string& source) {
  static const char kKeyword[] = "import";
  constexpr std::size_t kKeywordLen = sizeof(kKeyword) - 1;
  constexpr std::size_t kMaxImports = 8;
  auto boundary = [](char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return !std::isalnum(u) && u != '_';
  };

  std::vector<std::string> out;
  for (std::size_t at = source.find(kKeyword); at != std::string::npos;
       at = source.find(kKeyword, at + kKeywordLen)) {
    if (out.size() >= kMaxImports) break;
    if (at > 0 && !boundary(source[at - 1])) continue;
    std::size_t p = at + kKeywordLen;
    if (p >= source.size() || !boundary(source[p])) continue;
    while (p < source.size() && (source[p] == ' ' || source[p] == '\t')) ++p;
    std::size_t end = p;
    while (end < source.size() && !boundary(source[end])) ++end;
    if (end == p) continue;
    std::string name = source.substr(p, end - p);
    if (isReservedModule(name)) continue;
    if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
  }
  return out;
}

}

ScriptHost::ScriptHost(AppRegistry& registry, ScriptServices& services, AppHook onInstalled,
                       AppHook onRemoved)
    : registry_(registry),
      svc_(services),
      httpAdapter_(this),
      mqttAdapter_(this),
      onInstalled_(std::move(onInstalled)),
      onRemoved_(std::move(onRemoved)) {
  activate();
  if (!installBindings(vm_, vmError_)) {
    if (svc_.log) svc_.log("[script] engine unavailable: " + vmError_);
  }
}

ScriptHost::~ScriptHost() {
  setServices(nullptr);
  for (const auto& kv : apps_) registry_.remove(kv.first);
}

// The bindings hold one global service pointer, so several hosts (device plus tests) would
// otherwise fight over it. Re-run before every entry into the VM to claim it back.
void ScriptHost::activate() {
  effective_ = svc_;
  effective_.http = &httpAdapter_;
  effective_.mqtt = &mqttAdapter_;
  effective_.shared = &shared_;
  setServices(&effective_);
}


// Sits between the script binding and the real transport to record who asked. The owner is
// taken from the active BindingScope, never from the script, so it cannot be spoofed.
bool ScriptHost::HttpAdapter::request(const HttpRequest& req) {
  ScriptHost* h = host_;
  const std::string owner = BindingScope::currentScript();
  if (owner.empty()) return false;

  std::size_t pending = 0;
  for (const auto& kv : h->httpOwner_)
    if (kv.second.script == owner) ++pending;
  if (pending >= kMaxPendingHttp) return false;

  if (!h->svc_.http) return false;

  if (!h->svc_.http->request(req)) return false;
  const int64_t now = h->svc_.monotonicMs ? h->svc_.monotonicMs() : 0;
  h->httpOwner_[req.id] = HttpOwner{owner, now + kHttpTimeoutMs};
  return true;
}

void ScriptHost::MqttAdapter::publish(const std::string& topic, const std::string& payload) {
  if (host_->svc_.mqtt) host_->svc_.mqtt->publish(topic, payload);
}

void ScriptHost::MqttAdapter::subscribe(const std::string& topic) {
  ScriptHost* h = host_;
  const std::string owner = BindingScope::currentScript();
  if (owner.empty()) return;

  auto& subs = h->mqttSubs_[topic];
  if (std::find(subs.begin(), subs.end(), owner) != subs.end()) return;
  subs.push_back(owner);
  // One broker subscription per topic no matter how many scripts want it; the list is what
  // fans a delivery back out. Dropped again in forgetScript() when the last owner goes.
  if (subs.size() == 1 && h->svc_.mqtt) h->svc_.mqtt->subscribe(topic);
}


void ScriptHost::reportHeap(const std::string& name, std::size_t vmBefore,
                            std::size_t freeBefore) const {
  if (!svc_.log) return;
  const std::size_t vmNow = vm_.heapBytes();
  const long delta = static_cast<long>(vmNow) - static_cast<long>(vmBefore);
  std::string msg = "[script:" + name + "] vm heap " + (delta >= 0 ? "+" : "") +
                    std::to_string(delta) + " bytes (shared " + std::to_string(vmNow) + ")";
  if (svc_.freeHeap)
    msg += ", free heap " + std::to_string(freeBefore) + " -> " +
           std::to_string(svc_.freeHeap());
  if (svc_.maxAllocHeap) msg += ", max block " + std::to_string(svc_.maxAllocHeap());
  svc_.log(msg);
  if (const std::size_t low = heap::installLowWater())
    svc_.log("[script:" + name + "] install low-water " + std::to_string(low) + " bytes");
}

void ScriptHost::reportFrameTimes() {
  if (!svc_.logDebug) return;
  for (auto& kv : apps_) {
    const long us = kv.second->takePeakDrawUs();
    if (us <= 0) continue;
    std::string line = "[script:" + kv.first + "] draw peak " + std::to_string(us) +
                       " us (frame budget 25000 us), vm " + std::to_string(vm_.heapBytes());
    if (svc_.freeHeap) line += ", free " + std::to_string(svc_.freeHeap());
    if (svc_.maxAllocHeap) line += ", max block " + std::to_string(svc_.maxAllocHeap());
    svc_.logDebug(line);
  }
}


// Installs or replaces a script, app or module alike. False means the install was refused
// (see lastRefusal()); a script that compiles and then throws still counts as installed.
bool ScriptHost::set(const std::string& name, const std::string& source,
                     const std::string& storeJson) {
  if (name.empty()) return false;
  lastRefusal_.clear();
  refusalTransient_ = false;
  refusalInvalid_ = false;

  const ScriptMeta meta = parseMeta(source);
  if (meta.module && refuseModule(name, meta)) return false;

  const bool isNew = !has(name);
  if (isNew && count() >= limit_) {
    lastRefusal_ = "script limit reached (" + std::to_string(count()) + " installed)";
    return false;
  }

  if (svc_.freeHeap) {
    const std::size_t need = installNeedsBytes(source.size(), !isNew);
    const std::size_t have = svc_.freeHeap();
    if (have < need) {
      refusalTransient_ = !httpOwner_.empty();
      lastRefusal_ = "not enough free memory to compile (" + std::to_string(have) +
                     " bytes free, needs " + std::to_string(need) + "); " +
                     (refusalTransient_ ? "a script fetch is in flight, try again"
                                        : "remove a script or reboot");
      if (svc_.log) svc_.log("[script] " + name + ": " + lastRefusal_);
      return false;
    }
  }

  // Free bytes are not enough on their own: the lexer needs the source in one contiguous
  // allocation, and a long-running device fragments.
  if (svc_.maxAllocHeap) {
    const std::size_t block = svc_.maxAllocHeap();
    if (block < source.size()) {
      lastRefusal_ = "heap too fragmented to compile (largest contiguous block " +
                     std::to_string(block) + " bytes, source is " +
                     std::to_string(source.size()) + "); remove a script or reboot";
      if (svc_.log) svc_.log("[script] " + name + ": " + lastRefusal_);
      return false;
    }
  }

  const heap::Info hi = heap::info();
  if (isNew && vm_.heapBytes() > hi.budgetBytes) {
    lastRefusal_ = "shared Berry heap " + std::to_string(vm_.heapBytes()) + " bytes is over the " +
                   std::to_string(hi.budgetBytes) + " byte " + hi.name +
                   " budget; remove a script";
    if (svc_.log && !heapWarned_) {
      svc_.log("[script] " + lastRefusal_ + "; refusing new scripts until it drops");
      heapWarned_ = true;
    }
    return false;
  }

  activate();
  const bool wasApp = apps_.count(name) != 0;
  if (wasApp) {
    purge(name);
    vm_.dropApp(name);
  }
  auto stale = modules_.find(name);
  if (stale != modules_.end()) {
    vm_.dropModule(stale->second.importName);
    modules_.erase(stale);
  }

  std::vector<std::string> imports = collectImports(source);
  if (imports.empty())
    imports_.erase(name);
  else
    imports_[name] = std::move(imports);

  vm_.gcCollect();

  // Modules have settings too, so the defaults are seeded before the branch:
  // both an app and a module read their store the moment their body runs.
  std::string seeded;
  const std::string* store = &storeJson;
  if (meta.hasConfig && seedConfigDefaults(parseConfig(source), storeJson, seeded))
    store = &seeded;

  if (meta.module) {
    if (wasApp) {
      registry_.remove(name);
      apps_.erase(name);
      if (onRemoved_) onRemoved_(name);
    }
    return installModule(name, meta, source, *store);
  }

  const std::size_t vmBefore = vm_.heapBytes();
  const std::size_t freeBefore = svc_.freeHeap ? svc_.freeHeap() : 0;

  std::unique_ptr<ScriptApp> app;
  {
    // Compiling is the peak allocation. The reserve makes the Berry allocator refuse rather
    // than eat the last of the heap, so a script too big to load fails instead of the device.
    heap::InstallReserve reserve(kInstallReserveBytes);
    app = std::make_unique<ScriptApp>(vm_, name, source, meta, *store, lastCtx());
  }
  vm_.gcCollect();
  reportHeap(name, vmBefore, freeBefore);
  drainStoreFlush();

  ScriptApp* raw = app.get();
  registry_.add(raw);
  apps_[name] = std::move(app);
  meta_[name] = meta;
  if (onInstalled_) onInstalled_(name);
  return true;
}

bool ScriptHost::refuseModule(const std::string& name, const ScriptMeta& meta) {
  const std::string& importName = meta.moduleName.empty() ? name : meta.moduleName;
  if (!isIdentifier(importName)) {
    lastRefusal_ = "module name '" + importName +
                   "' is not a valid identifier; use letters, digits and _";
  } else if (isReservedModule(importName)) {
    lastRefusal_ = "module name '" + importName + "' is reserved by a built-in module";
  } else {
    for (const auto& kv : modules_) {
      if (kv.first != name && kv.second.importName == importName) {
        lastRefusal_ = "module name '" + importName + "' is already used by " + kv.first;
        break;
      }
    }
  }
  if (lastRefusal_.empty()) return false;
  refusalInvalid_ = true;
  if (svc_.log) svc_.log("[script] " + name + ": " + lastRefusal_);
  return true;
}

bool ScriptHost::installModule(const std::string& name, const ScriptMeta& meta,
                               const std::string& source, const std::string& storeJson) {
  const std::size_t vmBefore = vm_.heapBytes();
  const std::size_t freeBefore = svc_.freeHeap ? svc_.freeHeap() : 0;

  Module mod;
  mod.importName = meta.moduleName.empty() ? name : meta.moduleName;
  bool ok = false;
  {
    heap::InstallReserve reserve(kInstallReserveBytes);
    BindingScope scope(nullptr, lastCtx(), name);
    // Loaded before the module body runs, and under the module's own scope --
    // which is what lets a module read its settings at top level. That cache
    // cannot go stale: saving settings reinstalls the module, and
    // reloadDependents() below restarts everything that imports it.
    if (!storeJson.empty()) vm_.call1("_store_load", storeJson);
    ok = vm_.loadModule(mod.importName, source);
  }
  if (!ok) mod.error = parseScriptError(vm_.lastError());

  vm_.gcCollect();
  reportHeap(name, vmBefore, freeBefore);
  drainStoreFlush();

  const std::string importName = mod.importName;
  modules_[name] = std::move(mod);
  meta_[name] = meta;
  reloadDependents(importName, name);
  return true;
}

// Reinstalls every script importing importName from its stored source. Modules go to the
// front of the list: an app must not reload against a dependency that is itself still stale.
void ScriptHost::reloadDependents(const std::string& importName, const std::string& skip) {
  if (!svc_.readSource || reloadDepth_ >= kMaxReloadDepth) return;

  std::vector<std::string> dependents;
  for (const auto& kv : imports_) {
    if (kv.first == skip) continue;
    if (std::find(kv.second.begin(), kv.second.end(), importName) == kv.second.end()) continue;
    if (modules_.count(kv.first))
      dependents.insert(dependents.begin(), kv.first);
    else
      dependents.push_back(kv.first);
  }
  if (dependents.empty()) return;

  ++reloadDepth_;
  std::string source, storeJson;
  for (const std::string& dep : dependents) {
    source.clear();
    if (!svc_.readSource(dep, source)) continue;
    storeJson.clear();
    if (svc_.readStore) svc_.readStore(dep, storeJson);
    if (!set(dep, source, storeJson) && svc_.log)
      svc_.log("[script] " + dep + " still runs the previous " + importName + ": " +
               lastRefusal_);
  }
  --reloadDepth_;
}

void ScriptHost::remove(const std::string& name) {
  auto mod = modules_.find(name);
  if (mod != modules_.end()) {
    const std::string importName = mod->second.importName;
    vm_.dropModule(importName);
    // A module has a store of its own now, so it needs the same release an app
    // gets -- otherwise its settings outlive the file in the VM.
    purge(name);
    modules_.erase(mod);
    meta_.erase(name);
    imports_.erase(name);
    vm_.gcCollect();
    if (heapWarned_ && vm_.heapBytes() <= heap::info().budgetBytes) heapWarned_ = false;
    reloadDependents(importName, std::string());
    return;
  }

  auto it = apps_.find(name);
  if (it == apps_.end()) return;
  registry_.remove(name);
  purge(name);
  vm_.dropApp(name);
  apps_.erase(it);
  meta_.erase(name);
  imports_.erase(name);
  vm_.gcCollect();
  if (heapWarned_ && vm_.heapBytes() <= heap::info().budgetBytes) heapWarned_ = false;
  if (onRemoved_) onRemoved_(name);
}

// Releases both halves of a script's state: what the host tracks and what the prelude holds.
void ScriptHost::purge(const std::string& name) {
  forgetScript(name);
  vm_.call1("_app_forget", name);
}

// The host-side half: shared values, in-flight request ownership and mqtt subscriptions.
void ScriptHost::forgetScript(const std::string& name) {
  shared_.purge(name);

  for (auto it = httpOwner_.begin(); it != httpOwner_.end();)
    it = (it->second.script == name) ? httpOwner_.erase(it) : std::next(it);

  for (auto it = mqttSubs_.begin(); it != mqttSubs_.end();) {
    auto& subs = it->second;
    subs.erase(std::remove(subs.begin(), subs.end(), name), subs.end());
    if (subs.empty()) {
      if (svc_.mqtt) svc_.mqtt->unsubscribeAll(it->first);
      it = mqttSubs_.erase(it);
    } else {
      ++it;
    }
  }
}


// store.set() only parks the serialised map in the binding layer; this is what carries it to
// the sink. Has to run after every VM entry, or the write is still pending at the next one.
void ScriptHost::drainStoreFlush() {
  if (!BindingScope::storeFlushPending()) return;
  const BindingScope::StoreFlush f = BindingScope::takeStoreFlush();
  if (svc_.storeSink && !f.script.empty()) svc_.storeSink->storeChanged(f.script, f.json);
}

void ScriptHost::drainHttp(const RenderCtx* ctx) {
  HttpResult r;
  while (httpQueue_.pop(r)) {
    auto it = httpOwner_.find(r.id);
    if (it == httpOwner_.end()) continue;
    const std::string script = it->second.script;
    httpOwner_.erase(it);
    auto app = apps_.find(script);
    if (app == apps_.end() || !active(script)) continue;
    app->second->dispatchHttp(r.id, r.status, r.body, r.ok, ctx);
    drainStoreFlush();
  }
}

// Fails over-due requests by hand. A transport that never answers would otherwise leave the
// callback pending forever and hold one of the script's kMaxPendingHttp slots.
void ScriptHost::sweepHttp(const RenderCtx* ctx) {
  if (httpOwner_.empty()) return;
  const int64_t now = svc_.monotonicMs ? svc_.monotonicMs() : 0;

  std::vector<std::pair<uint32_t, std::string>> expired;
  for (auto it = httpOwner_.begin(); it != httpOwner_.end();) {
    if (now - it->second.dueMs >= 0) {
      expired.push_back({it->first, it->second.script});
      it = httpOwner_.erase(it);
    } else {
      ++it;
    }
  }

  for (const auto& e : expired) {
    auto app = apps_.find(e.second);
    if (app == apps_.end() || !active(e.second)) continue;
    app->second->dispatchHttp(e.first, 0, std::string(), false, ctx);
    drainStoreFlush();
  }
}

void ScriptHost::drainMqtt(const RenderCtx* ctx) {
  MqttMessage m;
  while (mqttQueue_.pop(m)) {
    const std::string& key = m.filter.empty() ? m.topic : m.filter;
    auto it = mqttSubs_.find(key);
    if (it == mqttSubs_.end()) continue;
    const std::vector<std::string> targets = it->second;
    for (const auto& name : targets) {
      auto app = apps_.find(name);
      if (app == apps_.end() || !active(name)) continue;
      app->second->dispatchMqtt(key, m.topic, m.payload, ctx);
      drainStoreFlush();
    }
  }
}

void ScriptHost::updateVisibility(const std::string& currentAppId,
                                  const std::string& incomingAppId, const RenderCtx* ctx) {
  for (auto& kv : apps_) {
    const bool shown = kv.first == currentAppId || kv.first == incomingAppId;
    kv.second->notifyVisible(shown, ctx);
    drainStoreFlush();
  }
}

void ScriptHost::tick(const RenderCtx& ctx, const std::string& currentAppId,
                      const std::string& incomingAppId) {
  activate();
  lastCtx_ = ctx;
  haveCtx_ = true;
  drainStoreFlush();

  drainHttp(&ctx);
  sweepHttp(&ctx);
  drainMqtt(&ctx);

  // loop() runs once a second for every script, not once per frame, and the stagger set at
  // boot keeps them from all landing on the same tick.
  const int64_t now = svc_.monotonicMs ? svc_.monotonicMs() : 0;
  if (now - lastLoopMs_ >= 1000) {
    lastLoopMs_ = now;
    for (auto& kv : apps_) {
      if (!active(kv.first)) continue;
      if (now < kv.second->loopNotBeforeMs()) continue;
      kv.second->tickLoop(ctx);
      drainStoreFlush();
    }
    reportFrameTimes();
  }

  updateVisibility(currentAppId, incomingAppId, &ctx);
}

void ScriptHost::staggerFirstLoops(int64_t stepMs) {
  const int64_t now = svc_.monotonicMs ? svc_.monotonicMs() : 0;
  int64_t offset = 0;
  for (auto& kv : apps_) {
    kv.second->holdLoopUntil(now + offset);
    offset += stepMs;
  }
}

void ScriptHost::setRunningScripts(std::vector<std::string> running) {
  running_ = std::move(running);
  runningKnown_ = true;
}

// An installed script that is not in the rotation still exists but must not be driven.
// Before the first setRunningScripts() nothing is known, so treat everything as running.
bool ScriptHost::active(const std::string& name) const {
  if (!runningKnown_) return true;
  return std::find(running_.begin(), running_.end(), name) != running_.end();
}

bool ScriptHost::isHeadless(const std::string& name) const {
  const auto it = meta_.find(name);
  return it != meta_.end() && it->second.headless;
}

bool ScriptHost::wantsShow(const std::string& name) {
  auto it = apps_.find(name);
  if (it == apps_.end()) return true;
  activate();
  const bool want = it->second->wantsShow(lastCtx());
  drainStoreFlush();
  return want;
}

long ScriptHost::durationMs(const std::string& name) const {
  auto it = apps_.find(name);
  return it == apps_.end() ? 0 : it->second->dwellMs();
}

bool ScriptHost::scrollHolds(const std::string& name) const {
  auto it = apps_.find(name);
  return it != apps_.end() && it->second->scrollHolds();
}

void ScriptHost::handleButton(const std::string& currentAppId, const std::string& btn) {
  auto it = apps_.find(currentAppId);
  if (it == apps_.end()) return;
  activate();
  it->second->handleButton(btn, lastCtx());
  drainStoreFlush();
}


ScriptError ScriptHost::errorOf(const std::string& name) const {
  auto mod = modules_.find(name);
  if (mod != modules_.end()) return mod->second.error;
  auto it = apps_.find(name);
  if (it == apps_.end()) return ScriptError();
  return it->second->ok() ? ScriptError() : it->second->error();
}

std::vector<SharedEntry> ScriptHost::sharedSnapshot() const {
  return snapshot(shared_, svc_.monotonicMs ? svc_.monotonicMs() : 0);
}

std::map<std::string, ScriptHost::Info> ScriptHost::list() const {
  std::map<std::string, Info> out;
  auto describe = [&](const std::string& name, Info& info) {
    auto m = meta_.find(name);
    if (m == meta_.end()) return;
    info.headless = m->second.headless;
    info.config = m->second.hasConfig;
    info.metaName = m->second.name;
    info.desc = m->second.desc;
    info.author = m->second.author;
    info.version = m->second.version;
  };
  for (const auto& kv : apps_) {
    Info info;
    info.error = kv.second->ok() ? ScriptError() : kv.second->error();
    info.skipping = !kv.second->lastWantedShow();
    describe(kv.first, info);
    out[kv.first] = std::move(info);
  }
  for (const auto& kv : modules_) {
    Info info;
    info.error = kv.second.error;
    info.module = true;
    info.importName = kv.second.importName;
    describe(kv.first, info);
    // A module never draws, so `headless` says nothing about it. `config` does:
    // its settings are the point of importing it.
    info.headless = false;
    out[kv.first] = std::move(info);
  }
  return out;
}

}
