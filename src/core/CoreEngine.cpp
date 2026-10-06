#include "core/CoreEngine.h"
#include "platform_apps/Policy.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "core/api/JsonCoerce.h"
#include "core/api/JsonText.h"
#include "core/apps/BuiltinAppConfig.h"
#include "core/effects/EffectRegistry.h"
#include "core/payload/PayloadParser.h"
#include "core/StrCase.h"
#include "core/sound/SoundSpec.h"
#include "system/HeapProbe.h"

namespace awtrix {

CoreEngine::CoreEngine(sound::AudioRouter& audio, IDisplayService& display, ISystemService& system,
                       std::vector<std::string> builtins)
    : notifs_(kMaxNotifications), bus_(kCommandQueueDepth), builtinApps_(std::move(builtins)), audio_(audio), display_(display),
      system_(system) {
  // A script app may decline its turn, so the rotation skips past it instead of showing a blank.
  appHost_.setShowGate([this](const std::string& id) {
    return !scripts_ || !isScriptApp(id) || scripts_->scriptWantsShow(id);
  });
  state_.subscribe([this, previous = state_.settings().scrollDefaults](StateEvent event) mutable {
    if (event != StateEvent::SettingsChanged) return;
    const auto& next = state_.settings().scrollDefaults;
    if (previous.mode != next.mode || previous.direction != next.direction ||
        previous.entry != next.entry || previous.whenFits != next.whenFits ||
        previous.speed != next.speed || previous.gap != next.gap || previous.holdMs != next.holdMs)
      resetContentCompletion(false);
    previous = next;
  });
  rebuildAppList();
}

void CoreEngine::resetContentCompletion(bool assets) {
  for (auto& entry : pushedApps_) {
    auto& prepared = entry.spec.extras().content;
    if (!prepared) continue;
    if (assets) prepared->invalidateAssets();
    else prepared->restart();
    if (entry.name == currentAppId()) {
      rotationPassesDonePage_.clear();
      rotationPassesDoneRevision_ = 0;
      // Keep an unfinished repeating page alive until its next rendered result.
      rotationHold_ = endsOnScrollPasses(entry.spec, false);
    }
  }
  if (notifs_.hasCurrent() && notifs_.current().extras().content) {
    notifPassesDone_ = false;
    notificationHold_ = endsOnScrollPasses(notifs_.current(), true);
  }
  notifs_.invalidateContent(assets);
}

void CoreEngine::invalidateContentAssets() { resetContentCompletion(true); }

DispatchResult CoreEngine::execute(Command& c) {
  CommandContext ctx{state_, *this, *this, audio_, display_, system_};
  ctx.scripts = scripts_;
  ctx.stations = this;
  ctx.overlays = overlays_;
  const DispatchResult r = dispatcher_.dispatch(c, ctx);
  lastDetail_ = ctx.detail;
  return r;
}

// Runs once per main loop, before the frame is rendered: queued commands first so their effect is
// visible in the same frame, then app lifetimes, then the rotation clock.
void CoreEngine::tick(int64_t nowMs) {
  now_ = nowMs;
  Command c;
  while (bus_.pop(c)) execute(c);
  if (sessionEndRequested_) endSession();
  // An expired pushed app either disappears or stays put with lifeTimeEnd set, which the renderer
  // marks with a red border.
  bool anyDead = false;
  for (auto& e : pushedApps_) {
    AppSpec& sp = e.spec;
    if (sp.lifetimeMs <= 0) continue;
    const int64_t elapsed = nowMs - e.receivedAtMs;
    if (elapsed < sp.lifetimeMs) continue;
    if (sp.lifetimeExpiry == LifetimeExpiry::Remove)
      anyDead = true;
    else
      sp.lifeTimeEnd = true;
  }
  if (anyDead) {
    pushedApps_.erase(
        std::remove_if(pushedApps_.begin(), pushedApps_.end(),
                       [nowMs](const PushedAppEntry& e) {
                         return e.spec.lifetimeMs > 0 &&
                                e.spec.lifetimeExpiry == LifetimeExpiry::Remove &&
                                nowMs - e.receivedAtMs >= e.spec.lifetimeMs;
                       }),
        pushedApps_.end());
    rebuildAppList();
  }
  notifs_.update(nowMs, state_.settings().appDurationMs, notificationHold_,
                 notifPassesDone_ && notifPassesDoneGen_ == notifs_.generation());
  // How long the current page stays up: its own duration wins over the global default, and a page
  // that has finished its requested scroll passes gets zero, which rotates on the next tick.
  long dwellMs = state_.settings().appDurationMs;
  const std::string& cur = appHost_.currentId();
  if (const AppSpec* cs = pushedApp(cur)) {
    if (cs->durationMs > 0) dwellMs = cs->durationMs;
    else if (!cur.empty() && rotationPassesDonePage_ == cur &&
             (!cs->extras().content || cs->extras().content->revision() == rotationPassesDoneRevision_)) dwellMs = 0;
  } else if (scripts_ && isScriptApp(cur)) {
    const long d = scripts_->scriptDurationMs(cur);
    if (d > 0) dwellMs = d;
  }
  const bool holds = rotationHold_ || scriptRotationPaused_ ||
                     (scripts_ && isScriptApp(cur) && scripts_->scriptScrollHolds(cur));
  appHost_.tick(nowMs, dwellMs, state_.settings().transitionDurationMs,
                state_.settings().autoTransition && !holds);
}

std::vector<CoreEngine::PushedAppEntry>::iterator CoreEngine::pushedLowerBound(
    const std::string& name) {
  return std::lower_bound(pushedApps_.begin(), pushedApps_.end(), name,
                          [](const PushedAppEntry& e, const std::string& n) { return e.name < n; });
}

std::vector<CoreEngine::PushedAppEntry>::const_iterator CoreEngine::pushedLowerBound(
    const std::string& name) const {
  return std::lower_bound(pushedApps_.begin(), pushedApps_.end(), name,
                          [](const PushedAppEntry& e, const std::string& n) { return e.name < n; });
}

const AppSpec* CoreEngine::pushedApp(const std::string& name) const {
  auto it = pushedLowerBound(name);
  return (it == pushedApps_.end() || it->name != name) ? nullptr : &it->spec;
}

namespace {
bool sensorPresent(const RuntimeState& rt, const std::string& app) {
  if (app == "Temperature") return rt.hasTemperature;
  if (app == "Humidity") return rt.hasHumidity;
  if (app == "Battery") return rt.hasBattery;
  return true;
}
}

// Apps that exist right now: the built-ins this hardware can actually fill, plus pushed and
// script apps. This is also the fallback loop order for anything the user has not arranged.
bool CoreEngine::hasBuiltin(const std::string& name) const {
  return std::find(builtinApps_.begin(), builtinApps_.end(), name) != builtinApps_.end() &&
         sensorPresent(state_.runtime(), name) && !pushedApp(name);
}

std::vector<std::string> CoreEngine::knownApps() const {
  std::vector<std::string> k;
  for (const auto& n : builtinApps_)
    if (hasBuiltin(n)) k.push_back(n);
  std::vector<const PushedAppEntry*> byArrival;
  byArrival.reserve(pushedApps_.size());
  for (const auto& e : pushedApps_) byArrival.push_back(&e);
  std::sort(byArrival.begin(), byArrival.end(),
            [](const PushedAppEntry* a, const PushedAppEntry* b) {
              return a->arrival < b->arrival;
            });
  for (const PushedAppEntry* e : byArrival) k.push_back(e->name);
  for (const auto& n : scriptApps_) k.push_back(n);
  return k;
}

// knownApps plus names that only live on in the saved arrangement, so the UI keeps listing an app
// whose sender is currently away.
std::vector<std::string> CoreEngine::allApps() const {
  std::vector<std::string> k = knownApps();
  auto remember = [&k](const std::vector<std::string>& names) {
    for (const auto& n : names)
      if (std::find(k.begin(), k.end(), n) == k.end()) k.push_back(n);
  };
  remember(order_);
  remember(disabled_);
  return k;
}

int CoreEngine::slotOf(const std::string& name) const {
  const auto it = std::find(order_.begin(), order_.end(), name);
  return it == order_.end() ? -1 : static_cast<int>(it - order_.begin());
}

bool CoreEngine::isPresent(const std::string& name) const {
  const std::vector<std::string> k = knownApps();
  return std::find(k.begin(), k.end(), name) != k.end();
}

std::string CoreEngine::appOrderJson() const {
  std::string out = "{\"order\":[";
  for (std::size_t i = 0; i < order_.size(); ++i) {
    if (i) out += ',';
    api::appendJsonString(out, order_[i]);
  }
  out += "],\"disabled\":[";
  for (std::size_t i = 0; i < disabled_.size(); ++i) {
    if (i) out += ',';
    api::appendJsonString(out, disabled_[i]);
  }
  out += "]}";
  return out;
}

DispatchResult CoreEngine::setBuiltinAppConfig(const std::string& name, const std::string& json,
                                              DispatchDetail& detail) {
  if (!hasBuiltin(name)) {
    detail.message = "no such app";
    return DispatchResult::NotFound;
  }
  bool changed = false;
  const DispatchResult result =
      builtinconfig::applyPatch(name, json, clockFaces(), state_.settings(), changed, detail);
  if (changed) state_.emit(StateEvent::SettingsChanged);
  return result;
}

void CoreEngine::dropRetired(std::vector<std::string>& names) const {
  names.erase(std::remove_if(names.begin(), names.end(),
                             [this](const std::string& n) {
                               return retiredBuiltin(n, builtinApps_) &&
                                      !pushedApp(n) &&
                                      std::find(scriptApps_.begin(), scriptApps_.end(), n) ==
                                          scriptApps_.end();
                             }),
              names.end());
}

void CoreEngine::syncScriptApp(const std::string& name) {
  if (name.empty()) return;
  const bool onDemand = scripts_ && scripts_->scriptIsOnDemand(name);
  std::vector<std::string>& into = onDemand ? onDemandApps_ : scriptApps_;
  std::vector<std::string>& from = onDemand ? scriptApps_ : onDemandApps_;
  from.erase(std::remove(from.begin(), from.end(), name), from.end());
  if (std::find(into.begin(), into.end(), name) == into.end()) into.push_back(name);
  // Saved without @ondemand while it runs: it simply stays on as a rotation app.
  if (!onDemand && session_ == name) forgetSession();
  const bool arranged = onDemand && forgetArrangement(name);
  rebuildAppList();
  if (arranged && orderSaveFn_) orderSaveFn_(appOrderJson());
}

bool CoreEngine::forgetArrangement(const std::string& name) {
  const auto gone = [&name](const std::string& n) { return n == name; };
  const auto o = std::remove_if(order_.begin(), order_.end(), gone);
  const auto h = std::remove_if(disabled_.begin(), disabled_.end(), gone);
  const bool changed = o != order_.end() || h != disabled_.end();
  order_.erase(o, order_.end());
  disabled_.erase(h, disabled_.end());
  return changed;
}

void CoreEngine::removeScriptApp(const std::string& name) {
  const auto onDemand = std::find(onDemandApps_.cbegin(), onDemandApps_.cend(), name);
  if (onDemand != onDemandApps_.cend()) {
    onDemandApps_.erase(onDemand);
    if (session_ == name) leaveSession();
    return;
  }
  const auto it = std::find(scriptApps_.cbegin(), scriptApps_.cend(), name);
  if (it == scriptApps_.cend()) return;
  scriptApps_.erase(it);
  const bool arranged = forgetArrangement(name);
  rebuildAppList();
  if (arranged && orderSaveFn_) orderSaveFn_(appOrderJson());
}

// Recomputes the loop after anything appears or disappears: arranged apps in their saved order
// first, newcomers appended. Headless scripts still run, they are just not handed to the display.
void CoreEngine::rebuildAppList() {
  const std::vector<std::string> known = knownApps();
  std::vector<std::string> running;
  for (const auto& n : order_) {
    if (std::find(known.begin(), known.end(), n) != known.end() && isEnabled(n))
      running.push_back(n);
  }
  for (const auto& n : known) {
    if (isEnabled(n) && std::find(running.begin(), running.end(), n) == running.end())
      running.push_back(n);
  }
  std::vector<std::string> drawn;
  drawn.reserve(running.size());
  for (const auto& n : running) {
    if (!scripts_ || !scripts_->scriptIsHeadless(n)) drawn.push_back(n);
  }
  // A session has the panel to itself; headless scripts keep running underneath it.
  if (!session_.empty()) {
    running.push_back(session_);
    drawn.assign(1, session_);
  }
  appHost_.setApps(drawn);
  if (scripts_) scripts_->setRunningScripts(running);
}

bool CoreEngine::validateSpecNames(const AppSpec& spec, DispatchDetail& detail) const {
  if (effects_ && !spec.effect.empty() && !effects_->find(spec.effect)) {
    detail = {"effect", "unknown effect"};
    return false;
  }
  if (overlays_ && !spec.overlay.empty() && !overlays_->find(spec.overlay)) {
    detail = {"overlay", "unknown overlay"};
    return false;
  }
  if (fonts_ && !spec.font.empty() && !fonts_->find(spec.font)) {
    detail = {"font", "unknown font"};
    return false;
  }
  return true;
}

bool CoreEngine::prepareSpecContent(AppSpec& spec, DispatchDetail& detail) {
  const auto& content = spec.extras().content;
  return !content || content->prepare(detail);
}

DispatchResult CoreEngine::setPushedApp(const std::string& name, const std::string& json,
                                        DispatchDetail& detail) {
  {
    api::JsonReader probeReader{std::string_view(json)};
    if (!probeReader.skipValue() || !probeReader.atEnd()) return DispatchResult::ParseError;
  }
  probe::report("exec:doc", 128);
  probe::begin();

  struct Parsed {
    std::string key;
    std::string arrayBase;
    AppSpec spec;
  };
  std::vector<Parsed> parsed;
  auto parseOne = [&](const std::string& key, api::JsonReader obj, const std::string& base) {
    AppSpec sp;
    if (!payload::readAppSpec(obj, false, sp, &detail)) return false;
    sp.name = key;
    if (!validateSpecNames(sp, detail)) return false;
    if (!prepareSpecContent(sp, detail)) return false;
    parsed.push_back(Parsed{key, base, std::move(sp)});
    return true;
  };

  // An array payload becomes name0, name1, ... Each entry remembers the base name so deleting
  // "name" later takes the whole set with it.
  api::JsonReader root{std::string_view(json)};
  if (root.isArray()) {
    api::JsonReader arr = root;
    int idx = 0;
    if (arr.enterArray()) {
      while (arr.nextElement()) {
        if (arr.isObject() && !parseOne(name + std::to_string(idx++), arr, name))
          return DispatchResult::ValidationError;
        if (!arr.skipValue()) break;
      }
    }
  } else if (root.isObject()) {
    if (!parseOne(name, root, std::string())) return DispatchResult::ValidationError;
  } else {
    return DispatchResult::ParseError;
  }

  probe::report("exec:parse", 128);
  probe::begin();

  // Only names we do not hold yet count against the limit; overwriting an existing app is free.
  // Nothing has been stored so far, so bailing out here leaves the whole batch unapplied.
  const std::size_t free =
      pushedApps_.size() >= kMaxPushedApps ? 0 : kMaxPushedApps - pushedApps_.size();
  std::size_t needed = 0;
  for (const auto& p : parsed)
    if (!pushedApp(p.key)) ++needed;
  if (needed > free) return DispatchResult::Capacity;

  for (auto& p : parsed) {
    auto it = pushedLowerBound(p.key);
    if (it != pushedApps_.end() && it->name == p.key) {
      if (p.spec.extras().content && it->spec.extras().content)
        p.spec.extras().content->inheritState(*it->spec.extras().content);
      if (p.key == appHost_.currentId() && (p.spec.extras().content || it->spec.extras().content)) {
        rotationHold_ = false;
        rotationPassesDonePage_.clear();
      }
      it->spec = std::move(p.spec);
      it->receivedAtMs = now_;
      it->arrayBase = p.arrayBase;
    } else {
      pushedApps_.insert(
          it, PushedAppEntry{p.key, std::move(p.spec), now_, p.arrayBase, nextArrival_++});
    }
  }
  rebuildAppList();
  probe::report("exec:store", 128);
  probe::begin();
  return DispatchResult::Ok;
}

void CoreEngine::deletePushedApp(const std::string& name) {
  pushedApps_.erase(std::remove_if(pushedApps_.begin(), pushedApps_.end(),
                                   [&](const PushedAppEntry& e) {
                                     return e.name == name || e.arrayBase == name;
                                   }),
                    pushedApps_.end());
  rebuildAppList();
}

namespace {
bool readNames(api::JsonReader r, std::vector<std::string>& out, bool once) {
  if (!r.enterArray()) return false;
  while (r.nextElement()) {
    std::string name;
    if (!r.isString() || !r.appendString(name) || name.empty()) return false;
    if (!once || std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
    if (!r.skipValue()) return false;
  }
  return r.ok();
}
}

bool CoreEngine::setAppOrder(const std::string& json) {
  if (!api::isWellFormed(json)) return false;
  api::JsonReader root{std::string_view(json)};
  if (!root.isObject()) return false;

  api::JsonReader on{std::string_view(json)};
  api::JsonReader off{std::string_view(json)};
  bool namedOrder = false, namedHidden = false;
  api::JsonReader o = root;
  if (!o.enterObject()) return false;
  while (o.nextMember()) {
    if (o.keyEquals("order")) {
      if (!o.isArray()) return false;
      on = o;
      namedOrder = true;
    } else if (o.keyEquals("disabled")) {
      if (!o.isArray()) return false;
      off = o;
      namedHidden = true;
    }
    if (!o.skipValue()) return false;
  }
  // "disabled" has to be there, "order" is optional — leaving it out keeps the current sequence.
  if (!o.ok() || !namedHidden) return false;

  std::vector<std::string> order = order_;
  if (namedOrder) {
    order.clear();
    if (!readNames(on, order, false)) return false;
  }
  std::vector<std::string> disabled;
  if (!readNames(off, disabled, true)) return false;
  dropRetired(order);
  dropRetired(disabled);

  order_ = std::move(order);
  disabled_ = std::move(disabled);
  rebuildAppList();
  if (orderSaveFn_) orderSaveFn_(appOrderJson());
  return true;
}

// One app on or off, the rest of the arrangement untouched. Switched off, an app keeps its place in
// the order, so switching it back on returns it there.
void CoreEngine::setAppEnabled(const std::string& name, bool enabled) {
  std::vector<std::string> named{name};
  dropRetired(named);
  if (named.empty()) return;
  const auto it = std::find(disabled_.begin(), disabled_.end(), name);
  if ((it == disabled_.end()) == enabled) return;
  if (enabled)
    disabled_.erase(it);
  else
    disabled_.push_back(name);
  rebuildAppList();
  if (orderSaveFn_) orderSaveFn_(appOrderJson());
}

DispatchResult CoreEngine::startSession(const std::string& name, DispatchDetail& detail) {
  if (!scripts_ || !isOnDemandApp(name)) {
    detail = {"name", "no such on-demand script"};
    return DispatchResult::NotFound;
  }
  if (session_ == name) return DispatchResult::Ok;
  endSession();
  // Already the session while init() and setup() run, so rotation.close() works from there too.
  sessionReturn_ = appHost_.currentId();
  session_ = name;
  const DispatchResult r = scripts_->launchScript(name, detail);
  if (r != DispatchResult::Ok) {
    forgetSession();
    return r;
  }
  scriptRotationPaused_ = false;
  rebuildAppList();
  appHost_.switchTo(name, now_);
  return DispatchResult::Ok;
}

void CoreEngine::endSession() {
  if (session_.empty()) return;
  if (scripts_) scripts_->unloadScript(session_);
  leaveSession();
}

bool CoreEngine::requestSessionEnd(const std::string& name) {
  if (name.empty() || name != session_) return false;
  sessionEndRequested_ = true;
  return true;
}

// Hands the panel back to the rotation, at the app that was showing when the session began.
void CoreEngine::leaveSession() {
  const std::string back = sessionReturn_;
  forgetSession();
  scriptRotationPaused_ = false;
  rebuildAppList();
  appHost_.switchTo(back, now_);
}

void CoreEngine::forgetSession() {
  session_.clear();
  sessionReturn_.clear();
  sessionEndRequested_ = false;
}

// Takes a bare app name or {"name":..,"fast":true}, where fast skips the transition animation.
// Any explicit navigation also releases a rotation hold a script had taken, and ends a session
// unless it names an on-demand script, which starts one.
DispatchResult CoreEngine::switchApp(const std::string& nameOrJson, DispatchDetail& detail) {
  scriptRotationPaused_ = false;
  std::string name = nameOrJson;
  bool fast = false;
  if (!nameOrJson.empty() && nameOrJson[0] == '{') {
    api::JsonReader r{std::string_view(nameOrJson)};
    api::JsonReader probe = r;
    if (probe.skipValue() && probe.atEnd() && r.enterObject()) {
      std::string parsed;
      while (r.nextMember()) {
        if (r.keyEquals("name") && r.isString() && r.appendString(parsed)) {
          name = parsed;
        } else if (r.keyEquals("fast")) {
          bool b = false;
          if (r.asBool(b)) fast = b;
        }
        if (!r.skipValue()) break;
      }
    }
  }
  if (isOnDemandApp(name)) return startSession(name, detail);
  endSession();
  const bool found = fast ? appHost_.switchTo(name, now_) : appHost_.transitionTo(name, now_);
  return found ? DispatchResult::Ok : DispatchResult::NotFound;
}

void CoreEngine::nextApp() {
  endSession();
  scriptRotationPaused_ = false;
  appHost_.next(now_);
}
void CoreEngine::previousApp() {
  endSession();
  scriptRotationPaused_ = false;
  appHost_.previous(now_);
}

DispatchResult CoreEngine::notify(const std::string& json, uint8_t ,
                                  DispatchDetail& detail) {
  return pushNotification(json, std::string(), detail);
}

DispatchResult CoreEngine::notifyFromScript(const std::string& script, const std::string& json,
                                            DispatchDetail& detail) {
  return pushNotification(json, script, detail);
}

DispatchResult CoreEngine::pushNotification(const std::string& json,
                                            const std::string& soundScript,
                                            DispatchDetail& detail) {
  AppSpec spec;
  int elements = 0;
  payload::JsonParse why = payload::JsonParse::Ok;
  if (!payload::parse(json, true, spec, &elements, &why, &detail)) {
    if (why != payload::JsonParse::Ok) return payload::toDispatchResult(why);
    return DispatchResult::ValidationError;
  }
  if (elements > 1) {
    detail = {"", "one notification per request"};
    return DispatchResult::ValidationError;
  }
  if (!validateSpecNames(spec, detail)) return DispatchResult::ValidationError;
  if (!spec.sound.empty()) {
    sound::Choices choices;
    sound::parse(spec.sound, sound::Origin::Notification, choices, detail);
    if (!audio_.check(choices, sound::Origin::Notification, detail))
      return DispatchResult::ValidationError;
  }
  if (!prepareSpecContent(spec, detail)) return DispatchResult::ValidationError;
  if (!soundScript.empty() && !spec.sound.empty()) spec.extrasMut().soundScript = soundScript;
  if (!notifs_.push(spec, now_)) return DispatchResult::Capacity;
  return DispatchResult::Ok;
}

void CoreEngine::dismiss() { notifs_.dismiss(now_); }

bool CoreEngine::dismissNamed(const std::string& name) { return notifs_.dismissNamed(name, now_); }

DispatchResult CoreEngine::setStations(const std::string& json, DispatchDetail& detail) {
  std::vector<radio::Station> parsed;
  radio::StationError error;
  if (!radio::parseStations(json, parsed, error)) {
    detail.field = error.index >= 0 ? "stations[" + std::to_string(error.index) + "]." + error.field
                                    : error.field;
    detail.message = error.message;
    return error.message == "invalid JSON" ? DispatchResult::ParseError
                                           : DispatchResult::ValidationError;
  }
  std::sort(parsed.begin(), parsed.end(), [](const radio::Station& x, const radio::Station& y) {
    return strcase::alphaLess(x.name, y.name);
  });
  stations_.swap(parsed);
  if (stationSaveFn_) stationSaveFn_(radio::stationsToJson(stations_));
  return DispatchResult::Ok;
}

std::string CoreEngine::stationUrl(const std::string& name) const {
  const int index = radio::indexOfStation(stations_, name);
  return index < 0 ? std::string() : stations_[index].url;
}

std::string CoreEngine::stationNameAt(int index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= stations_.size()) return std::string();
  return stations_[index].name;
}
}
