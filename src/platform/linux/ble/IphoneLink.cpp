#include "platform/linux/ble/IphoneLink.h"

#include <algorithm>
#include <utility>

#include "core/api/JsonCoerce.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

namespace awtrix::ble {
namespace {

using api::JsonReader;
using api::JsonWriter;

bool isFailure(const std::string& reply) { return api::present(api::memberValue(reply, "error")); }

JsonWriter& peer(JsonWriter& w, const Address& a) { return w.key("addr").value(a.str()).key("random").value(a.random); }

}

const char* iphoneStateName(IphoneState state) {
  switch (state) {
    case IphoneState::Off: return "off";
    case IphoneState::Waiting: return "waiting";
    case IphoneState::Connecting: return "connecting";
    case IphoneState::Ready: return "ready";
  }
  return "off";
}

IphoneLink::IphoneLink(BleHub& hub, std::string name, std::function<void(const std::string&)> log)
    : hub_(hub), name_(std::move(name)), log_(std::move(log)) {}

void IphoneLink::configure(const IphoneConfig& config, int64_t nowMs) {
  now_ = nowMs;
  // A phone is only ever taken back by forget(): the one remembered here may not have reached the
  // main loop yet.
  if (config.havePhone) {
    haveKnown_ = true;
    known_ = config.phone;
  }
  if (config.enabled == enabled_) return;
  enabled_ = config.enabled;
  if (enabled_) {
    failures_ = 0;
    advertise();
    return;
  }
  const bool linked = state_ == IphoneState::Connecting || state_ == IphoneState::Ready;
  stopAll();
  if (linked) hub_.disconnect(central_);
  publish(IphoneState::Off);
  clearMusic();
}

void IphoneLink::forget(int64_t nowMs) {
  now_ = nowMs;
  relinkAt_ = -1;
  if (!haveKnown_) return;
  std::string args;
  peer(JsonWriter(args).beginObject(), known_).endObject();
  call("forget", args, 0);
  hub_.disconnect(known_);
  haveKnown_ = false;
  known_ = Address{};
}

void IphoneLink::onEvent(BleEvent event) { events_.push_back(std::move(event)); }

void IphoneLink::tick(int64_t nowMs) {
  now_ = nowMs;
  std::deque<BleEvent> events;
  events.swap(events_);
  for (const BleEvent& e : events) handle(e);
  if (requestId_ && nowMs >= requestUntil_) {
    if (log_) log_("iphone: no answer for notification " + std::to_string(assembler_.result().uid));
    finishRequest();
    request();
  }
  if (advertiseAt_ >= 0 && nowMs >= advertiseAt_) {
    advertiseAt_ = -1;
    if (enabled_ && !advertId_) advertise();
  }
  if (relinkAt_ >= 0 && nowMs >= relinkAt_) {
    relinkAt_ = -1;
    if (enabled_ && state_ == IphoneState::Waiting) link(central_);
  }
}

IphoneStatus IphoneLink::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

std::deque<IphoneEvent> IphoneLink::take() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::deque<IphoneEvent> out;
  out.swap(out_);
  return out;
}

void IphoneLink::stopAll() {
  hub_.forget(kOwner, now_);
  advertId_ = 0;
  advertiseAt_ = -1;
  relinkAt_ = -1;
  connId_ = dataId_ = noticeId_ = updateId_ = trackSetupId_ = playerSetupId_ = nameId_ = 0;
  pending_.clear();
  finishRequest();
}

// Asks for the notification service rather than offering one: that is what makes an iPhone list
// the display under its Bluetooth devices and offer to share its notifications with it.
void IphoneLink::advertise() {
  if (state_ == IphoneState::Off) publish(IphoneState::Waiting);
  std::string args;
  JsonWriter(args).beginObject().key("name").value(name_).key("solicit").beginArray().value(ancs::kService)
      .endArray().endObject();
  advertId_ = next();
  const std::string reply = call("advertise", args, advertId_);
  if (isFailure(reply)) advertFailed(api::memberText(reply, "error"));
}

// The controller turns an advert down now and then, right after a restart above all: it is asked
// again a moment later, and the same reason is logged once.
void IphoneLink::advertFailed(const std::string& why) {
  if (log_ && why != advertError_) log_("iphone: cannot advertise: " + why);
  advertError_ = why;
  advertId_ = 0;
  advertiseAt_ = now_ + kRetryMs;
}

void IphoneLink::link(const Address& central) {
  central_ = central;
  // The phone connects to us; a link to it that has gone again is not opened from this side.
  if (!hub_.linked(central)) return;
  publish(IphoneState::Connecting);
  std::string args;
  peer(JsonWriter(args).beginObject(), central).endObject();
  connId_ = next();
  if (isFailure(call("connect", args, connId_))) lost("cannot connect");
}

// Data Source before Notification Source, so no answer can come before its subscription. The
// player's writes queue behind the subscriptions on the link, and pairing retries them in the
// same order, so the answer to the last one says every subscription is in place.
void IphoneLink::setUp() {
  bool notifications = false;
  hasMusic_ = false;
  Uuid ancsUuid, amsUuid;
  Uuid::parse(ancs::kService, ancsUuid);
  Uuid::parse(ams::kService, amsUuid);
  const std::string reply = call("services", "{\"conn\":" + std::to_string(connId_) + "}", 0);
  JsonReader list = api::memberValue(reply, "services");
  if (list.enterArray()) {
    while (list.nextElement()) {
      std::string text;
      Uuid u;
      if (api::memberValue(list, "uuid").appendString(text) && Uuid::parse(text, u)) {
        notifications = notifications || u == ancsUuid;
        hasMusic_ = hasMusic_ || u == amsUuid;
      }
      if (!list.skipValue()) break;
    }
  }
  if (!notifications) {
    if (log_) log_("iphone: " + central_.str() + " offers no notifications");
    release();
    publish(IphoneState::Waiting);
    return;
  }
  dataId_ = next();
  noticeId_ = next();
  if (isFailure(call("subscribe", target(ancs::kService, ancs::kDataSource), dataId_)) ||
      isFailure(call("subscribe", target(ancs::kService, ancs::kNotificationSource), noticeId_))) {
    lost("cannot subscribe");
    return;
  }
  if (!hasMusic_) {
    ready();
    return;
  }
  updateId_ = next();
  if (isFailure(call("subscribe", target(ams::kService, ams::kEntityUpdate), updateId_))) {
    lost("cannot subscribe");
    return;
  }
  const auto write = [this](const Bytes& value, uint32_t id) {
    return !isFailure(call("write", target(ams::kService, ams::kEntityUpdate, value), id));
  };
  trackSetupId_ = next();
  playerSetupId_ = next();
  if (!write(ams::trackAttributes(), trackSetupId_) || !write(ams::playerAttributes(), playerSetupId_))
    lost("cannot ask for the player");
}

void IphoneLink::ready() {
  failures_ = 0;
  publish(IphoneState::Ready);
  if (log_) log_("iphone: " + central_.str() + " ready");
  if (!haveKnown_ || known_.b != central_.b) {
    nameId_ = next();
    if (isFailure(call("read", target("1800", "2a00"), nameId_))) remember("");
  }
  request();
}

void IphoneLink::remember(const std::string& name) {
  haveKnown_ = true;
  known_ = central_;
  IphoneEvent e;
  e.kind = IphoneEvent::Kind::Phone;
  e.phone = central_;
  e.name = name;
  emit(std::move(e));
}

void IphoneLink::lost(const std::string& why) {
  if (log_) log_("iphone: " + why);
  release();
  publish(enabled_ ? IphoneState::Waiting : IphoneState::Off);
  clearMusic();
  // Still linked, the phone will not connect again by itself: this side tries once more, slower
  // after every failure in a row.
  if (enabled_ && hub_.linked(central_)) {
    relinkAt_ = now_ + std::min(kRetryMaxMs, kRetryMs << std::min(failures_, 5));
    ++failures_;
  }
}

void IphoneLink::release() {
  if (connId_) call("disconnect", "{\"conn\":" + std::to_string(connId_) + "}", 0);
  connId_ = dataId_ = noticeId_ = updateId_ = trackSetupId_ = playerSetupId_ = nameId_ = 0;
  pending_.clear();
  finishRequest();
}

void IphoneLink::handle(const BleEvent& e) {
  const std::string error = api::memberText(e.json, "error");
  if (e.id == advertId_ && advertId_) {
    if (!error.empty() || e.done) {
      advertFailed(error.empty() ? "stopped" : error);
      return;
    }
    if (api::present(api::memberValue(e.json, "central"))) onCentral(e);
    return;
  }
  if (e.id == connId_ && connId_) {
    if (!error.empty() || api::present(api::memberValue(e.json, "disconnected"))) lost(error.empty() ? "disconnected" : error);
    else if (api::memberFlag(e.json, "connected")) setUp();
    return;
  }
  if ((e.id == dataId_ && dataId_) || (e.id == noticeId_ && noticeId_) || (e.id == updateId_ && updateId_)) {
    if (!error.empty() || e.done) {
      lost(error.empty() ? "subscription ended" : error);
      return;
    }
    Bytes value;
    if (!fromHex(api::memberText(e.json, "data"), value)) return;
    if (e.id == dataId_) onData(value);
    else if (e.id == noticeId_) onNotice(value);
    else onUpdate(value);
    return;
  }
  if ((e.id == trackSetupId_ && trackSetupId_) || (e.id == playerSetupId_ && playerSetupId_)) {
    const bool last = e.id == playerSetupId_;
    if (last) playerSetupId_ = 0;
    else trackSetupId_ = 0;
    if (!error.empty()) lost(error);
    else if (last) ready();
    return;
  }
  if (e.id == nameId_ && nameId_) {
    nameId_ = 0;
    Bytes name;
    if (!error.empty() || !fromHex(api::memberText(e.json, "data"), name)) name.clear();
    std::string decoded(name.begin(), name.end());
    ancs::trimUtf8(decoded);
    remember(decoded);
    return;
  }
  if (e.id == requestId_ && requestId_ && !error.empty()) {
    if (log_) log_("iphone: notification " + std::to_string(assembler_.result().uid) + ": " + error);
    finishRequest();
    request();
  }
}

void IphoneLink::onCentral(const BleEvent& e) {
  Address a;
  if (!Address::parse(api::memberText(e.json, "central"), api::memberFlag(e.json, "random"), a)) return;
  if (!api::memberFlag(e.json, "connected")) {
    if (a.b != central_.b) return;
    relinkAt_ = -1;
    if (state_ == IphoneState::Connecting || state_ == IphoneState::Ready) lost("disconnected");
    return;
  }
  advertError_.clear();
  if (!enabled_ || state_ != IphoneState::Waiting) return;
  if (haveKnown_ && known_.b != a.b) return;
  failures_ = 0;
  link(a);
}

void IphoneLink::onNotice(const Bytes& value) {
  ancs::Notice n;
  if (!ancs::parseNotice(value, n)) return;
  if (n.event == ancs::kRemoved) {
    pending_.erase(std::remove(pending_.begin(), pending_.end(), n.uid), pending_.end());
    return;
  }
  if (!n.fresh()) return;
  if (pending_.size() >= kPendingMax) pending_.pop_front();
  pending_.push_back(n.uid);
  request();
}

void IphoneLink::onData(const Bytes& value) {
  const ancs::Assembler::Result result = assembler_.feed(value);
  if (result == ancs::Assembler::Result::Waiting) return;
  if (result == ancs::Assembler::Result::Done) {
    IphoneEvent e;
    e.kind = IphoneEvent::Kind::Notification;
    e.notification = assembler_.result();
    emit(std::move(e));
  }
  finishRequest();
  request();
}

void IphoneLink::onUpdate(const Bytes& value) {
  ams::Update update;
  if (ams::parseUpdate(value, update) && playback_.apply(update, now_)) emitMusic();
}

// One question on the Control Point at a time: the answers on the Data Source carry no marker
// that would tell two apart.
void IphoneLink::request() {
  while (!requestId_ && !pending_.empty() && state_ == IphoneState::Ready) {
    const uint32_t uid = pending_.front();
    pending_.pop_front();
    requestId_ = next();
    assembler_.begin(uid);
    requestUntil_ = now_ + kRequestTimeoutMs;
    const std::string args = target(ancs::kService, ancs::kControlPoint, ancs::attributesRequest(uid));
    if (isFailure(call("write", args, requestId_))) finishRequest();
  }
}

void IphoneLink::finishRequest() {
  requestId_ = 0;
  requestUntil_ = -1;
  assembler_.reset();
}

void IphoneLink::publish(IphoneState state) {
  state_ = state;
  std::lock_guard<std::mutex> lock(mutex_);
  status_.state = state;
  status_.notifications = state == IphoneState::Ready;
  status_.music = state == IphoneState::Ready && hasMusic_;
}

void IphoneLink::emit(IphoneEvent event) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (out_.size() >= kEventsMax) out_.pop_front();
  out_.push_back(std::move(event));
}

void IphoneLink::clearMusic() {
  if (playback_.player.empty() && playback_.title.empty() && !playback_.playing()) return;
  playback_ = ams::Playback{};
  emitMusic();
}

void IphoneLink::emitMusic() {
  IphoneEvent e;
  e.kind = IphoneEvent::Kind::Music;
  e.music = playback_;
  e.music.info.elapsed = playback_.elapsedAt(now_);
  e.music.elapsedAtMs = 0;
  emit(std::move(e));
}

std::string IphoneLink::call(const char* op, const std::string& args, uint32_t id) {
  return hub_.call(kOwner, op, args, id, now_);
}

std::string IphoneLink::target(const char* service, const char* characteristic, const Bytes& data) const {
  std::string out;
  JsonWriter w(out);
  w.beginObject().key("conn").value(static_cast<long long>(connId_)).key("svc").value(service)
      .key("chr").value(characteristic);
  if (!data.empty()) w.key("data").value(toHex(data));
  w.endObject();
  return out;
}

}
