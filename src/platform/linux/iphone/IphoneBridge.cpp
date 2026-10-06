#include "platform/linux/iphone/IphoneBridge.h"

#include <cmath>
#include <ctime>
#include <utility>

#include "core/api/ApiRouter.h"
#include "core/api/JsonWriter.h"
#include "platform/posix/Files.h"

namespace awtrix::iphone {
namespace {

constexpr const char* kNote8 =
    "data:image/gif;base64,R0lGODdhCAAIAIEAAAAAAPWlaAAAAAAAACwAAAAACAAIAAAIHQABCBxIMIDBgwIDFEy4EIDCgQcfRjSYkCLBgQEBADs=";
constexpr const char* kNote16 =
    "data:image/gif;base64,R0lGODdhEAAQAIEAAAAAAPWlaAAAAAAAACwAAAAAEAAQAEAIPQABCBxIsGDBAAEMCkRIkKFChBAhNoyYcCDFixUXZjy"
    "IcWJEhSBDitSIUaLFjRNTPtzo0GBJjyVZUhxZMCAAOw==";

}

const char* IphoneBridge::noteIcon(int size) { return size > 8 ? kNote16 : kNote8; }

IphoneBridge::IphoneBridge(ble::IphoneControl& link, INotifyService& notify, IAppService& apps, CoverSource* covers,
                           Options options)
    : link_(link), notify_(notify), apps_(apps), covers_(covers), options_(std::move(options)) {
  if (!options_.unixTime) options_.unixTime = [] { return static_cast<long long>(std::time(nullptr)); };
  std::string text;
  if (!options_.path.empty() && posix::readText(options_.path, text, Settings::kMaxFileBytes))
    settings_ = fromFile(text);
  ble::IphoneConfig config;
  config.enabled = settings_.enabled;
  config.havePhone = settings_.havePhone;
  config.phone = settings_.phone;
  link_.configureIphone(config);
}

int IphoneBridge::handle(const std::string& method, const std::string& path, const std::string& request,
                         std::string& body) {
  if (path == "/api/v1/iphone/phone") {
    if (method != "DELETE") {
      body = api::errorJson("methodNotAllowed", "allowed: DELETE");
      return 405;
    }
    link_.forgetIphone();
    settings_.havePhone = false;
    settings_.phone = ble::Address{};
    settings_.phoneName.clear();
    if (!save(settings_) && options_.log) options_.log("iphone: cannot store the settings in " + options_.path);
    body = this->body();
    return 200;
  }
  if (path != "/api/v1/iphone") return 0;
  if (method == "GET") {
    body = this->body();
    return 200;
  }
  if (method != "PUT") {
    body = api::errorJson("methodNotAllowed", "allowed: GET, PUT");
    return 405;
  }
  Settings next = settings_;
  bool malformed = false;
  std::string message, field;
  if (!applyUpdate(request, next, malformed, message, field)) {
    body = api::errorJson(malformed ? "invalidJson" : "validationFailed", message, field);
    return malformed ? 400 : 422;
  }
  if (!save(next)) {
    body = api::errorJson("insufficientStorage", "storage full");
    return 507;
  }
  const bool switched = next.enabled != settings_.enabled;
  settings_ = std::move(next);
  if (switched) {
    ble::IphoneConfig config;
    config.enabled = settings_.enabled;
    link_.configureIphone(config);
  }
  body = this->body();
  return 200;
}

void IphoneBridge::tick(int64_t nowMs) {
  for (const ble::IphoneEvent& e : link_.takeIphoneEvents()) {
    switch (e.kind) {
      case ble::IphoneEvent::Kind::Phone: onPhone(e); break;
      case ble::IphoneEvent::Kind::Notification: onNotification(e.notification, nowMs); break;
      case ble::IphoneEvent::Kind::Music:
        track_ = e.music;
        track_.elapsedAtMs = nowMs;
        // Title, artist and position arrive one by one: they are shown once the burst is over.
        if (settleAt_ < 0) settleAt_ = nowMs + kSettleMs;
        break;
    }
  }
  std::string url;
  if (covers_ && !lookupKey_.empty() && covers_->result(url)) {
    cache_.put(lookupKey_, std::move(url));
    lookupKey_.clear();
  }
  music(nowMs);
  if (seenDirty_ && nowMs - seenSavedAt_ >= kSeenSaveMs) {
    seenSavedAt_ = nowMs;
    save(settings_);
  }
}

void IphoneBridge::flush() {
  if (seenDirty_) save(settings_);
}

void IphoneBridge::onPhone(const ble::IphoneEvent& e) {
  settings_.havePhone = true;
  settings_.phone = e.phone;
  settings_.phoneName = e.name;
  if (options_.log) options_.log("iphone: paired with " + (e.name.empty() ? e.phone.str() : e.name));
  if (!save(settings_) && options_.log) options_.log("iphone: cannot store the settings in " + options_.path);
}

void IphoneBridge::onNotification(const ble::ancs::Attributes& a, int64_t nowMs) {
  if (a.app.empty()) return;
  settings_.saw(a.app, a.title, options_.unixTime());
  seenDirty_ = true;
  const AppRule* rule = settings_.rule(a.app);
  if (!rule) return;
  const std::string text = content(a.title, a.message);
  // The phone now and then announces one notification twice.
  const std::string key = a.app + '\0' + text;
  if (key == lastKey_ && nowMs - lastAt_ < kRepeatMs) return;
  lastKey_ = key;
  lastAt_ = nowMs;
  DispatchDetail detail;
  const std::string payload = notification(options_.panel, rule->name, text, rule->icon);
  if (notify_.notify(payload, static_cast<uint8_t>(Source::Internal), detail) != DispatchResult::Ok && options_.log)
    options_.log("iphone: notification refused: " + detail.message);
}

void IphoneBridge::music(int64_t nowMs) {
  if (!settings_.enabled || !settings_.music) {
    settleAt_ = -1;
    if (pushed_) removeMusic();
    return;
  }
  if (settleAt_ >= 0) {
    if (nowMs < settleAt_) return;
    settleAt_ = -1;
  }
  if (!track_.playing()) {
    if (!pushed_) return;
    if (stopAt_ < 0) stopAt_ = nowMs + kGraceMs;
    if (nowMs >= stopAt_) removeMusic();
    return;
  }
  stopAt_ = -1;
  bool waiting = false;
  const std::string icon = cover(nowMs, waiting);
  const std::string key = track_.player + '\0' + track_.title + '\0' + track_.artist;
  // A new track waits a moment for the cover on its way, so it does not change icon right away.
  if (waiting && key != shownKey_) return;
  Track t;
  t.title = track_.title;
  t.artist = track_.artist;
  t.durationMs = std::llround(track_.duration * 1000);
  t.positionMs = std::llround(track_.elapsedAt(nowMs) * 1000);
  const int leds = barLeds(t, barWidth(options_.panel));
  if (pushed_ && key == shownKey_ && icon == shownCover_ && leds == shownLeds_ && nowMs - pushedAt_ < kRefreshMs)
    return;
  const bool fresh = !pushed_ || key != shownKey_;
  pushed_ = true;
  pushedAt_ = nowMs;
  shownKey_ = key;
  shownCover_ = icon;
  shownLeds_ = leds;
  DispatchDetail detail;
  if (apps_.setPushedApp(kMusicApp, iphone::music(options_.panel, t, icon), detail) != DispatchResult::Ok) {
    if (options_.log) options_.log("iphone: now playing refused: " + detail.message);
    return;
  }
  if (fresh) apps_.switchApp(std::string("{\"name\":\"") + kMusicApp + "\"}", detail);
}

// The cover when it is known, else the icon of the app named like the player, else a note.
std::string IphoneBridge::cover(int64_t nowMs, bool& waiting) {
  const int size = options_.panel.iconSize();
  if (settings_.covers && covers_) {
    const std::string key = CoverCache::key(track_.artist, track_.title, size);
    std::string icon;
    if (cache_.find(key, icon)) {
      if (!icon.empty()) return icon;
    } else if (lookupKey_ == key) {
      waiting = nowMs - lookupSince_ < kCoverWaitMs;
    } else if (lookupKey_.empty() && covers_->lookup(track_.artist, track_.title, size)) {
      lookupKey_ = key;
      lookupSince_ = nowMs;
      waiting = true;
    }
  }
  for (const AppRule& app : settings_.apps)
    if (app.name == track_.player && !app.icon.empty()) return app.icon;
  return noteIcon(size);
}

void IphoneBridge::removeMusic() {
  apps_.deletePushedApp(kMusicApp);
  pushed_ = false;
  shownKey_.clear();
  shownCover_.clear();
  shownLeds_ = -1;
  stopAt_ = -1;
}

bool IphoneBridge::save(const Settings& settings) {
  if (options_.path.empty()) return true;
  if (!posix::replaceText(options_.path, toFile(settings))) return false;
  seenDirty_ = false;
  return true;
}

std::string IphoneBridge::body() const {
  const ble::IphoneStatus status = link_.iphoneStatus();
  std::string out;
  api::JsonWriter w(out);
  w.beginObject().key("enabled").value(settings_.enabled).key("state").value(ble::iphoneStateName(status.state))
      .key("phone");
  writePhone(w, settings_);
  w.key("notifications").value(status.notifications).key("music").value(status.music).key("settings");
  writeSettings(w, settings_);
  w.key("seen");
  writeSeen(w, settings_);
  w.endObject();
  return out;
}

}
