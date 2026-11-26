#include "core/remote/RemoteApps.h"

#include <algorithm>

#include "core/script/ScriptHeap.h"

namespace awtrix {
namespace remote {

namespace {
constexpr uint8_t kKind = wire::app::kHeadless | wire::app::kOnDemand;
}

void RemoteApp::render(Canvas& canvas, const RenderCtx& ctx) {
  (void)ctx;
  canvas.clear();
  frames_.draw(id_, canvas);
}

RemoteApps::RemoteApps(AppRegistry& registry, const RemoteFrames& frames, Notify added, Notify removed,
                       Hold hold)
    : registry_(registry), frames_(frames), added_(std::move(added)), removed_(std::move(removed)),
      hold_(std::move(hold)) {}

RemoteApps::~RemoteApps() {
  for (const auto& entry : entries_) registry_.remove(entry->app.id());
}

const RemoteApps::Entry* RemoteApps::find(std::string_view name) const {
  for (const auto& entry : entries_)
    if (entry->app.id() == name) return entry.get();
  return nullptr;
}

RemoteApps::Entry* RemoteApps::find(std::string_view name) {
  for (const auto& entry : entries_)
    if (entry->app.id() == name) return entry.get();
  return nullptr;
}

bool RemoteApps::has(std::string_view name, uint8_t flag) const {
  const Entry* entry = find(name);
  return entry && (entry->flags & flag);
}

void RemoteApps::apply(const wire::Apps& apps) {
  if (applied_ && apps.revision == revision_) return;
  applied_ = true;
  revision_ = apps.revision;
  auto listed = [&apps](const std::string& name) {
    for (uint8_t i = 0; i < apps.count; ++i)
      if (apps.entries[i].name == name) return true;
    return false;
  };
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (listed((*it)->app.id())) {
      ++it;
      continue;
    }
    const std::string name = (*it)->app.id();
    registry_.remove(name);
    it = entries_.erase(it);
    if (removed_) removed_(name);
  }
  for (uint8_t i = 0; i < apps.count; ++i) {
    const wire::AppEntry& wanted = apps.entries[i];
    Entry* entry = find(wanted.name);
    const bool known = entry != nullptr;
    if (!known) {
      entries_.push_back(std::unique_ptr<Entry>(new Entry{RemoteApp(std::string(wanted.name), frames_)}));
      entry = entries_.back().get();
      registry_.add(&entry->app);
    }
    const bool kindChanged = ((entry->flags ^ wanted.flags) & kKind) != 0;
    entry->flags = wanted.flags;
    entry->durationMs = wanted.durationMs;
    entry->slot = i;
    if ((!known || kindChanged) && added_) added_(entry->app.id());
  }
  maskOff();
}

void RemoteApps::setLinkUp(bool up) {
  linkUp_ = up;
  if (!up && held_) hold(false);
}

void RemoteApps::hold(bool on) {
  held_ = on && linkUp_;
  if (hold_) hold_(held_);
}

bool RemoteApps::takesButtons(const std::string& name) const {
  return linkUp_ && has(name, wire::app::kTakesButtons);
}

void RemoteApps::setRunningScripts(const std::vector<std::string>& running) {
  for (const auto& entry : entries_)
    entry->off = !(entry->flags & wire::app::kOnDemand) &&
                 std::find(running.begin(), running.end(), entry->app.id()) == running.end();
  maskOff();
}

void RemoteApps::maskOff() {
  offMask_ = 0;
  for (const auto& entry : entries_)
    if (entry->off && entry->slot < 32) offMask_ |= 1u << entry->slot;
}

DispatchResult RemoteApps::forward(const char* method, const std::string& path, const std::string& body,
                                   DispatchDetail& detail) {
  if (!linkUp_ || !forward_) {
    detail.message = "script host not reachable";
    return DispatchResult::OnHost;
  }
  if (body.size() > script::heap::growthBudget()) {
    detail.message = "not enough memory";
    return DispatchResult::Capacity;
  }
  if (!forward_(method, path, std::string(body))) {
    detail.message = "script host not reachable";
    return DispatchResult::OnHost;
  }
  return DispatchResult::Ok;
}

DispatchResult RemoteApps::setScript(const std::string& name, const std::string& source,
                                     DispatchDetail& detail) {
  return forward("PUT", "/api/v1/apps/script/" + name, source, detail);
}

DispatchResult RemoteApps::removeScript(const std::string& name, DispatchDetail& detail) {
  if (!owns(name)) return DispatchResult::Ok;
  return forward("DELETE", "/api/v1/apps/" + name, std::string(), detail);
}

DispatchResult RemoteApps::updateScript(const std::string& name, const std::string& json,
                                        DispatchDetail& detail) {
  return forward("PUT", "/api/v1/apps/script-update/" + name, json, detail);
}

DispatchResult RemoteApps::setScriptConfig(const std::string& name, const std::string& json,
                                           DispatchDetail& detail) {
  return forward("PATCH", "/api/v1/apps/" + name + "/config", json, detail);
}

DispatchResult RemoteApps::setScriptData(const std::string& name, const std::string& json,
                                         DispatchDetail& detail) {
  return forward("PATCH", "/api/v1/apps/" + name + "/data", json, detail);
}

bool RemoteApps::scriptWantsShow(const std::string& name) {
  return linkUp_ && has(name, wire::app::kWantsShow);
}

long RemoteApps::scriptDurationMs(const std::string& name) {
  const Entry* entry = find(name);
  return entry ? static_cast<long>(entry->durationMs) : 0;
}

bool RemoteApps::scriptScrollHolds(const std::string& name) {
  return linkUp_ && has(name, wire::app::kScrollHolds);
}

bool RemoteApps::scriptIsHeadless(const std::string& name) { return has(name, wire::app::kHeadless); }

bool RemoteApps::scriptIsOnDemand(const std::string& name) { return has(name, wire::app::kOnDemand); }

DispatchResult RemoteApps::launchScript(const std::string& name, DispatchDetail& detail) {
  if (!scriptIsOnDemand(name)) {
    detail = {"name", "no such on-demand script"};
    return DispatchResult::NotFound;
  }
  if (linkUp_) return DispatchResult::Ok;
  detail.message = "script host not reachable";
  return DispatchResult::OnHost;
}

}
}
