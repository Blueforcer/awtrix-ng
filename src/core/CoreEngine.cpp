#include "core/CoreEngine.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "core/api/JsonCoerce.h"
#include "core/api/JsonText.h"
#include "core/effects/EffectRegistry.h"
#include "core/payload/PayloadParser.h"
#include "system/HeapProbe.h"

namespace awtrix {

CoreEngine::CoreEngine(ISoundService& sound, IDisplayService& display, ISystemService& system)
    : notifs_(kMaxNotifications), bus_(kCommandQueueDepth), sound_(sound), display_(display),
      system_(system) {
  // A script app may decline its turn, so the rotation skips past it instead of showing a blank.
  appHost_.setShowGate([this](const std::string& id) {
    return !scripts_ || !isScriptApp(id) || scripts_->scriptWantsShow(id);
  });
  rebuildAppList();
}

DispatchResult CoreEngine::execute(const Command& c) {
  CommandContext ctx{state_, *this, *this, sound_, display_, system_};
  ctx.scripts = scripts_;
  ctx.radio = radio_;
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
    else if (!cur.empty() && rotationPassesDonePage_ == cur) dwellMs = 0;
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

// Apps that exist right now: the built-ins this hardware can actually fill, plus pushed and
// script apps.
std::vector<std::string> CoreEngine::knownApps() const {
  std::vector<std::string> k;
  k.push_back("Time");
  k.push_back("Date");
  const RuntimeState& rt = state_.runtime();
  if (rt.hasTemperature) k.push_back("Temperature");
  if (rt.hasHumidity) k.push_back("Humidity");
  if (rt.hasBattery) k.push_back("Battery");
  for (const auto& e : pushedApps_) k.push_back(e.name);
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
  remember(hidden_);
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
  out += "],\"hidden\":[";
  for (std::size_t i = 0; i < hidden_.size(); ++i) {
    if (i) out += ',';
    api::appendJsonString(out, hidden_[i]);
  }
  out += "]}";
  return out;
}

void CoreEngine::syncScriptApp(const std::string& name) {
  if (name.empty()) return;
  if (std::find(scriptApps_.begin(), scriptApps_.end(), name) == scriptApps_.end())
    scriptApps_.push_back(name);
  rebuildAppList();
}

bool CoreEngine::forgetArrangement(const std::string& name) {
  const auto gone = [&name](const std::string& n) { return n == name; };
  const auto o = std::remove_if(order_.begin(), order_.end(), gone);
  const auto h = std::remove_if(hidden_.begin(), hidden_.end(), gone);
  const bool changed = o != order_.end() || h != hidden_.end();
  order_.erase(o, order_.end());
  hidden_.erase(h, hidden_.end());
  return changed;
}

void CoreEngine::removeScriptApp(const std::string& name) {
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
  return true;
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
      it->spec = std::move(p.spec);
      it->receivedAtMs = now_;
      it->arrayBase = p.arrayBase;
    } else {
      pushedApps_.insert(it, PushedAppEntry{p.key, std::move(p.spec), now_, p.arrayBase});
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
    } else if (o.keyEquals("hidden")) {
      if (!o.isArray()) return false;
      off = o;
      namedHidden = true;
    }
    if (!o.skipValue()) return false;
  }
  // "hidden" has to be there, "order" is optional — leaving it out keeps the current sequence.
  if (!o.ok() || !namedHidden) return false;

  std::vector<std::string> order = order_;
  if (namedOrder) {
    order.clear();
    if (!readNames(on, order, false)) return false;
  }
  std::vector<std::string> hidden;
  if (!readNames(off, hidden, true)) return false;

  order_ = std::move(order);
  hidden_ = std::move(hidden);
  rebuildAppList();
  if (orderSaveFn_) orderSaveFn_(appOrderJson());
  return true;
}

// Takes a bare app name or {"name":..,"fast":true}, where fast skips the transition animation.
// Any explicit navigation also releases a rotation hold a script had taken.
bool CoreEngine::switchApp(const std::string& nameOrJson) {
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
  return fast ? appHost_.switchTo(name, now_) : appHost_.transitionTo(name, now_);
}

void CoreEngine::nextApp() {
  scriptRotationPaused_ = false;
  appHost_.next(now_);
}
void CoreEngine::previousApp() {
  scriptRotationPaused_ = false;
  appHost_.previous(now_);
}

DispatchResult CoreEngine::notify(const std::string& json, uint8_t ,
                                  DispatchDetail& detail) {
  AppSpec spec;
  int elements = 0;
  payload::JsonParse why = payload::JsonParse::Ok;
  if (!payload::parse(json, true, spec, &elements, &why, &detail)) {
    if (why != payload::JsonParse::Ok) return payload::toDispatchResult(why);
    return DispatchResult::ValidationError;
  }
  if (elements > 1) {
    detail = {"", "send one notification per request; an array of more than one is not accepted"};
    return DispatchResult::ValidationError;
  }
  if (!validateSpecNames(spec, detail)) return DispatchResult::ValidationError;
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
