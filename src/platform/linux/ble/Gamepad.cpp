#include "platform/linux/ble/Gamepad.h"

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

std::string target(uint32_t conn, const char* chr) {
  std::string out;
  JsonWriter(out).beginObject().key("conn").value(static_cast<long long>(conn)).key("svc").value("1812")
      .key("chr").value(chr).endObject();
  return out;
}

}

const char* gamepadStateName(GamepadStatus status) {
  switch (status) {
    case GamepadStatus::Unpaired: return "unpaired";
    case GamepadStatus::Pairing: return "pairing";
    case GamepadStatus::Waiting: return "waiting";
    case GamepadStatus::Connecting: return "connecting";
    case GamepadStatus::Ready: return "ready";
  }
  return "unpaired";
}

Gamepad::Gamepad(BleHub& hub, std::string owner, std::function<void(const std::string&)> log)
    : hub_(hub), owner_(std::move(owner)), log_(std::move(log)) {}

void Gamepad::remember(const Known& known) {
  haveKnown_ = true;
  known_ = known;
  publish(GamepadStatus::Waiting);
}

const Gamepad::Known* Gamepad::device() const {
  if (status_ == GamepadStatus::Connecting) return &linking_;
  return haveKnown_ ? &known_ : nullptr;
}

void Gamepad::start(int64_t nowMs) {
  now_ = nowMs;
  if (haveKnown_) watch();
}

void Gamepad::tick(int64_t nowMs) {
  now_ = nowMs;
  if (pairing_ && nowMs >= pairingUntil_) {
    pairing_ = false;
    if (log_) log_("gamepad: pairing timed out");
    retryAt_ = -1;
    watch();
  }
  if (retryAt_ >= 0 && nowMs >= retryAt_) {
    retryAt_ = -1;
    pairing_ ? searchNew() : watch();
  }
}

void Gamepad::pair(int64_t nowMs) {
  now_ = nowMs;
  pairing_ = true;
  pairingUntil_ = nowMs + kPairingMs;
  retryAt_ = -1;
  failures_ = 0;
  rejected_.clear();
  searchNew();
}

void Gamepad::forget(int64_t nowMs) {
  now_ = nowMs;
  stopAll();
  pairing_ = false;
  retryAt_ = -1;
  const bool had = haveKnown_;
  if (had) {
    std::string args;
    peer(JsonWriter(args).beginObject(), known_.address).endObject();
    hub_.call(owner_, "forget", args, 0, nowMs);
  }
  haveKnown_ = false;
  known_ = {};
  publish(GamepadStatus::Unpaired);
  if (had && onKnownChanged) onKnownChanged();
}

void Gamepad::stopAll() {
  hub_.forget(owner_, now_);
  scanId_ = connId_ = readId_ = subId_ = 0;
}

void Gamepad::searchNew() {
  stopAll();
  publish(GamepadStatus::Pairing);
  scanId_ = next();
  if (isFailure(call("scan", R"({"uuid":"1812","active":true,"dedupe":0})", scanId_))) lost("cannot search");
}

// A gamepad that is away is listened for in the background.
void Gamepad::watch() {
  stopAll();
  if (!haveKnown_) {
    publish(GamepadStatus::Unpaired);
    return;
  }
  publish(GamepadStatus::Waiting);
  std::string args;
  peer(JsonWriter(args).beginObject(), known_.address).key("dedupe").value(0).key("background").value(true).endObject();
  scanId_ = next();
  if (isFailure(call("scan", args, scanId_))) lost("cannot search");
}

void Gamepad::link(const Address& address, const std::string& name) {
  stopAll();
  linking_ = {address, name};
  publish(GamepadStatus::Connecting);
  std::string args;
  peer(JsonWriter(args).beginObject(), address).endObject();
  connId_ = next();
  if (isFailure(call("connect", args, connId_))) lost("cannot connect");
}

void Gamepad::lost(const std::string& why) {
  if (log_) log_("gamepad: " + why);
  stopAll();
  if (pairing_ && now_ >= pairingUntil_) pairing_ = false;
  publish(pairing_ ? GamepadStatus::Pairing : haveKnown_ ? GamepadStatus::Waiting : GamepadStatus::Unpaired);
  retryAt_ = now_ + std::min(kRetryMaxMs, kRetryMs << std::min(failures_, 5));
  ++failures_;
}

void Gamepad::handle(const BleEvent& e, int64_t nowMs) {
  now_ = nowMs;
  const std::string error = api::memberText(e.json, "error");
  if (e.id == scanId_ && scanId_) {
    if (!error.empty()) {
      lost(error);
      return;
    }
    if (e.done) return;
    Address a;
    if (!Address::parse(api::memberText(e.json, "addr"), api::memberFlag(e.json, "random"), a)) return;
    if (pairing_ && rejected_.count(a.b)) return;
    if (pairing_ && acceptPeer && !acceptPeer(a)) return;
    link(a, pairing_ ? api::memberText(e.json, "name") : known_.name);
    return;
  }
  if (e.id == connId_ && connId_) {
    if (!error.empty() || api::present(api::memberValue(e.json, "disconnected"))) {
      lost(error.empty() ? "disconnected" : error);
      return;
    }
    if (!api::memberFlag(e.json, "connected")) return;
    readId_ = next();
    if (isFailure(call("read", target(connId_, "2a4b"), readId_))) lost("cannot read the report map");
    return;
  }
  if (e.id == readId_ && readId_) {
    readId_ = 0;
    Bytes map;
    if (!error.empty() || !fromHex(api::memberText(e.json, "data"), map)) {
      lost(error.empty() ? "no report map" : error);
      return;
    }
    if (!parseHidReportMap(map, layout_)) {
      if (pairing_) rejected_.insert(linking_.address.b);
      lost(linking_.address.str() + " is not a gamepad");
      return;
    }
    subscribe();
    return;
  }
  if (e.id == subId_ && subId_) {
    if (!error.empty() || e.done) {
      lost(error.empty() ? "reports ended" : error);
      return;
    }
    hex_.clear();
    HidControls controls;
    if (!api::memberValue(e.json, "data").appendString(hex_) || !fromHex(hex_, report_) ||
        !readHidReport(report_, layout_, controls))
      return;
    if (status_ != GamepadStatus::Ready) ready();
    if (onControls) onControls(controls);
  }
}

// Only the Input Report characteristic that carries the gamepad's report: its Report Reference
// descriptor names the report id and the type, 1 for input.
void Gamepad::subscribe() {
  std::string args;
  JsonWriter w(args);
  w.beginObject().key("conn").value(static_cast<long long>(connId_)).key("svc").value("1812").key("chr").value("2a4d");
  if (layout_.reportId) {
    const Bytes reference{static_cast<uint8_t>(layout_.reportId), 1};
    w.key("desc").beginObject().key("2908").value(toHex(reference)).endObject();
  }
  w.endObject();
  subId_ = next();
  if (isFailure(call("subscribe", args, subId_))) lost("cannot subscribe");
}

void Gamepad::ready() {
  failures_ = 0;
  const bool changed = pairing_ || !haveKnown_ || known_.address.b != linking_.address.b;
  if (changed) {
    if (haveKnown_ && known_.address.b != linking_.address.b) {
      std::string args;
      peer(JsonWriter(args).beginObject(), known_.address).endObject();
      hub_.call(owner_, "forget", args, 0, now_);
    }
    pairing_ = false;
    haveKnown_ = true;
    known_ = linking_;
  }
  if (log_) log_("gamepad: " + (known_.name.empty() ? known_.address.str() : known_.name) + " ready");
  publish(GamepadStatus::Ready);
  if (changed && onKnownChanged) onKnownChanged();
}

void Gamepad::publish(GamepadStatus status) {
  status_ = status;
  if (onStatus) onStatus();
}

std::string Gamepad::call(const char* op, const std::string& args, uint32_t id) {
  return hub_.call(owner_, op, args, id, now_);
}

}
