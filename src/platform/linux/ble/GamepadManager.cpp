#include "platform/linux/ble/GamepadManager.h"

#include <algorithm>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/posix/Files.h"

namespace awtrix::ble {
namespace {

bool readKnown(api::JsonReader json, Gamepad::Known& known) {
  std::string address;
  bool random = false;
  const auto atRandom = api::memberValue(json, "random");
  const auto atName = api::memberValue(json, "name");
  return json.isObject() && api::memberValue(json, "addr").appendString(address) &&
         (!api::present(atRandom) || atRandom.asBool(random)) &&
         Address::parse(address, random, known.address) &&
         (!api::present(atName) || atName.appendString(known.name));
}

}

GamepadManager::GamepadManager(BleHub& hub, std::string path, std::function<void(const std::string&)> log)
    : path_(std::move(path)), log_(std::move(log)) {
  for (int i = 0; i < kGamepadPlayers; ++i) {
    sessions_[i] = std::make_unique<Gamepad>(hub, "@gamepad:" + std::to_string(i + 1), log_);
    Gamepad& session = *sessions_[i];
    session.acceptPeer = [this, i](const Address& address) {
      for (int other = 0; other < kGamepadPlayers; ++other)
        if (other != i && sessions_[other]->paired() && sessions_[other]->known().address.b == address.b) return false;
      return true;
    };
    session.onKnownChanged = [this] { save(); };
    session.onStatus = [this, i] { statusChanged(i); };
    session.onControls = [this, i](const HidControls& controls) {
      std::lock_guard<std::mutex> lock(mutex_);
      controls_[i] = controls;
    };
  }
  load();
}

bool GamepadManager::owns(const std::string& owner) const {
  return std::any_of(sessions_.begin(), sessions_.end(), [&](const auto& s) { return s->owner() == owner; });
}

void GamepadManager::onEvent(BleEvent event) { events_.push_back(std::move(event)); }

void GamepadManager::start(int64_t nowMs) {
  for (auto& session : sessions_) session->start(nowMs);
}

void GamepadManager::tick(int64_t nowMs) {
  // Only the events queued before this tick; those the sessions cause now wait for the next one.
  for (std::size_t n = events_.size(); n > 0; --n) {
    const BleEvent event = std::move(events_.front());
    events_.pop_front();
    for (auto& session : sessions_)
      if (session->owner() == event.script) {
        session->handle(event, nowMs);
        break;
      }
  }
  for (auto& session : sessions_) session->tick(nowMs);
}

GamepadPairResult GamepadManager::pair(int64_t nowMs) {
  for (int i = 0; i < kGamepadPlayers; ++i)
    if (sessions_[i]->pairing()) return {GamepadPairStatus::Started, i + 1};
  for (int i = 0; i < kGamepadPlayers; ++i)
    if (!sessions_[i]->paired()) {
      sessions_[i]->pair(nowMs);
      return {GamepadPairStatus::Started, i + 1};
    }
  return {GamepadPairStatus::Full, 0};
}

void GamepadManager::forget(int id, int64_t nowMs) {
  if (id >= 1 && id <= kGamepadPlayers) sessions_[id - 1]->forget(nowMs);
}

PlayerInput GamepadManager::input(int player) const {
  if (player < 1 || player > kGamepadPlayers) return {};
  std::lock_guard<std::mutex> lock(mutex_);
  const int slot = shown_[player - 1];
  const GamepadStatus state = devices_[slot].state;
  return {state, state == GamepadStatus::Ready ? controls_[slot] : HidControls{}};
}

std::string GamepadManager::name(int player) const {
  if (player < 1 || player > kGamepadPlayers) return {};
  std::lock_guard<std::mutex> lock(mutex_);
  return devices_[shown_[player - 1]].name;
}

GamepadDevices GamepadManager::devices() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return devices_;
}

void GamepadManager::statusChanged(int slot) {
  const Gamepad& session = *sessions_[slot];
  const bool ready = session.status() == GamepadStatus::Ready;
  const auto at = std::find(order_.begin(), order_.end(), slot);
  if (ready && at == order_.end()) {
    *std::find(order_.begin(), order_.end(), -1) = slot;
  } else if (!ready && at != order_.end()) {
    std::move(at + 1, order_.end(), at);
    order_.back() = -1;
  }
  GamepadDevice device;
  device.state = session.status();
  if (const Gamepad::Known* shown = session.device()) {
    device.name = shown->name;
    device.address = shown->address.str();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  devices_[slot] = std::move(device);
  if (!ready) controls_[slot] = {};
  for (auto& d : devices_) d.player = 0;
  for (int p = 0; p < kGamepadPlayers; ++p)
    if (order_[p] >= 0) {
      devices_[order_[p]].player = p + 1;
      shown_[p] = order_[p];
    }
  // A player without a Ready device shows the progress of one that is not: a paired one first.
  std::array<int, kGamepadPlayers> idle{};
  int count = 0;
  for (const bool empty : {false, true})
    for (int s = 0; s < kGamepadPlayers; ++s)
      if (!devices_[s].player && (devices_[s].state == GamepadStatus::Unpaired) == empty) idle[count++] = s;
  for (int p = 0, next = 0; p < kGamepadPlayers; ++p)
    if (order_[p] < 0) shown_[p] = idle[next++];
}

void GamepadManager::load() {
  std::string json;
  if (path_.empty() || !posix::readText(path_, json, 4096)) return;
  if (!api::isWellFormed(json) || !loadDevices(json)) {
    if (log_) log_("gamepad: invalid registry in " + path_);
  }
}

// A file without a version holds one device, read into slot 1; the next change writes the
// versioned form.
bool GamepadManager::loadDevices(const std::string& json) {
  const api::JsonReader root(json);
  const auto version = api::memberValue(root, "version");
  if (!api::present(version)) {
    Gamepad::Known known;
    if (!readKnown(root, known)) return false;
    sessions_[0]->remember(known);
    return true;
  }
  long long v = 0;
  if (!version.isInteger() || !version.asLong(v) || v != 2) return false;
  auto list = api::memberValue(root, "devices");
  if (!list.enterArray()) return false;
  std::array<Gamepad::Known, kGamepadPlayers> known;
  std::array<bool, kGamepadPlayers> seen{};
  while (list.nextElement()) {
    long long id = 0;
    const auto atId = api::memberValue(list, "id");
    if (!atId.isInteger() || !atId.asLong(id) || id < 1 || id > kGamepadPlayers || seen[id - 1] ||
        !readKnown(list, known[id - 1])) return false;
    seen[id - 1] = true;
    if (!list.skipValue()) return false;
  }
  if (!list.ok() || (seen[0] && seen[1] && known[0].address.b == known[1].address.b)) return false;
  for (int i = 0; i < kGamepadPlayers; ++i)
    if (seen[i]) sessions_[i]->remember(known[i]);
  return true;
}

// A failure is logged; the next pairing or removal writes the whole registry again.
void GamepadManager::save() {
  if (path_.empty()) return;
  std::string json;
  api::JsonWriter w(json);
  w.beginObject().key("version").value(2).key("devices").beginArray();
  for (int i = 0; i < kGamepadPlayers; ++i) {
    if (!sessions_[i]->paired()) continue;
    const auto& known = sessions_[i]->known();
    w.beginObject().key("id").value(i + 1).key("addr").value(known.address.str())
        .key("random").value(known.address.random).key("name").value(known.name).endObject();
  }
  w.endArray().endObject();
  if (!posix::replaceText(path_, json) && log_) log_("gamepad: cannot store registry in " + path_);
}

}
