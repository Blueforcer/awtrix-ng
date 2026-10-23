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

// By its name, or by Microsoft's company ID in the advert when it sends no name.
bool isXbox(const std::string& name, const std::string& advert) {
  if (name.find("Xbox") != std::string::npos) return true;
  JsonReader mfg = api::memberValue(advert, "mfg");
  if (!api::present(mfg) || !mfg.enterArray()) return false;
  while (mfg.nextElement()) {
    long long id = -1;
    if (api::memberValue(mfg, "id").asLong(id) && id == 0x0006) return true;
    if (!mfg.skipValue()) return false;
  }
  return false;
}

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
    if (log_) log_(std::string("gamepad: pairing timed out during ") + phase_);
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
  scanId_ = connId_ = pairId_ = readId_ = subId_ = 0;
}

void Gamepad::searchNew() {
  stopAll();
  phase_ = "scan";
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

void Gamepad::link(const Known& device) {
  stopAll();
  linking_ = device;
  phase_ = "connect/security/discovery";
  ignoredReports_ = 0;
  publish(GamepadStatus::Connecting);
  std::string args;
  const bool xbox = device.xbox;
  peer(JsonWriter(args).beginObject(), device.address).key("secure").value(xbox)
      .key("exchangeMtu").value(!xbox).key("steadyTiming").value(xbox).endObject();
  connId_ = next();
  if (isFailure(call("connect", args, connId_))) lost("cannot connect");
}

void Gamepad::lost(const std::string& why) {
  if (log_) log_(std::string("gamepad: ") + phase_ + ": " + why);
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
    Known device = known_;
    device.address = a;
    if (pairing_) {
      device.name = api::memberText(e.json, "name");
      device.xbox = isXbox(device.name, e.json);
    }
    link(device);
    return;
  }
  if (e.id == connId_ && connId_) {
    if (!error.empty() || api::present(api::memberValue(e.json, "disconnected"))) {
      lost(error.empty() ? "disconnected" : error);
      return;
    }
    if (!api::memberFlag(e.json, "connected")) return;
    // Pair before reading HID attributes, as Xbox controllers require an encrypted session.
    phase_ = "security";
    if (log_) log_("gamepad: " + linking_.address.str() + " GATT discovery complete; checking encryption");
    std::string args;
    JsonWriter(args).beginObject().key("conn").value(static_cast<long long>(connId_)).endObject();
    pairId_ = next();
    if (isFailure(call("pair", args, pairId_))) lost("cannot request pairing");
    return;
  }
  if (e.id == pairId_ && pairId_) {
    pairId_ = 0;
    const bool paired = error.empty() && api::memberFlag(e.json, "paired");
    // An Xbox stays silent without it; other gamepads may work without encryption, as before.
    if (!paired && linking_.xbox) {
      lost(error.empty() ? "pairing not confirmed" : error);
      return;
    }
    phase_ = "report map";
    if (log_) log_(paired ? "gamepad: encrypted; reading report map" : "gamepad: not paired; reading report map without encryption");
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
    if (log_) log_("gamepad: report map " + std::to_string(map.size()) + " bytes; input report " +
                   std::to_string(layout_.reportId) + " expects " + std::to_string(layout_.length) + " bytes");
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
    if (!api::memberValue(e.json, "data").appendString(hex_) || !fromHex(hex_, report_)) {
      if (ignoredReports_++ < 3 && log_) log_("gamepad: ignored malformed input data");
      return;
    }
    if (!readHidReport(report_, layout_, controls)) {
      if (ignoredReports_++ < 3 && log_)
        log_("gamepad: ignored input of " + std::to_string(report_.size()) + " bytes; expected " +
             std::to_string(layout_.length));
      return;
    }
    if (status_ != GamepadStatus::Ready) ready();
    if (onControls) onControls(controls);
  }
}

// Only the Input Report characteristic that carries the gamepad's report: its Report Reference
// descriptor names the report id and the type, 1 for input.
void Gamepad::subscribe() {
  phase_ = "subscription/first input";
  if (log_) log_("gamepad: subscribing to input report " + std::to_string(layout_.reportId));
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
  phase_ = "ready";
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
