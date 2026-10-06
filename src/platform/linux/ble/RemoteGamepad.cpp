#include "platform/linux/ble/RemoteGamepad.h"

#include <sys/random.h>

#include <cerrno>

namespace awtrix::ble {
namespace {

bool randomToken(RemoteGamepad::Token& token) {
  std::size_t filled = 0;
  while (filled < token.size()) {
    const ssize_t n = getrandom(token.data() + filled, token.size() - filled, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    filled += static_cast<std::size_t>(n);
  }
  return true;
}

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
         static_cast<uint32_t>(p[3]) << 24;
}

bool validPlayer(int player) { return player >= 1 && player <= kGamepadPlayers; }

}

uint32_t RemoteGamepad::start(const std::string& name, int player, Token& token) {
  if (!validPlayer(player) || !randomToken(token)) return 0;
  if (!socket_.isOpen() && !socket_.open(kPort)) return 0;
  return begin(name, player, token);
}

uint32_t RemoteGamepad::begin(const std::string& name, int player, const Token& token) {
  if (!validPlayer(player)) return 0;
  std::lock_guard<std::mutex> lock(mutex_);
  Slot& slot = slots_[player - 1];
  slot = Slot{};
  slot.id = nextId_++;
  if (!nextId_) nextId_ = 1;
  slot.token = token;
  slot.name = name;
  slot.heardAt = clock_();
  return slot.id;
}

bool RemoteGamepad::end(uint32_t id) {
  bool found = false, any = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Slot& slot : slots_) {
      if (id && slot.id == id) {
        slot = Slot{};
        found = true;
      }
      any = any || slot.id;
    }
  }
  if (!any) socket_.close();
  return found;
}

void RemoteGamepad::endAll() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Slot& slot : slots_) slot = Slot{};
  }
  socket_.close();
}

bool RemoteGamepad::receive(const uint8_t* data, std::size_t size) {
  if (size != kPacketBytes || data[0] != kVersion) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  for (Slot& slot : slots_) {
    if (!slot.id) continue;
    uint8_t differs = 0;
    for (std::size_t i = 0; i < slot.token.size(); ++i) differs |= data[1 + i] ^ slot.token[i];
    if (differs) continue;
    const uint32_t seq = le32(data + 17);
    if (slot.haveSeq && seq <= slot.seq) return false;
    slot.haveSeq = true;
    slot.seq = seq;
    slot.heardAt = clock_();
    slot.controls.buttons = le32(data + 21);
    const int hat = static_cast<int8_t>(data[25]);
    slot.controls.hat = hat >= 0 && hat <= 7 ? hat : -1;
    for (int i = 0; i < 4; ++i) slot.controls.axes[i] = data[26 + i];
    slot.controls.triggers[0] = data[30];
    slot.controls.triggers[1] = data[31];
    return true;
  }
  return false;
}

void RemoteGamepad::poll() {
  if (socket_.isOpen()) {
    uint8_t packet[kPacketBytes + 1];
    net::Endpoint from;
    for (int i = 0; i < 64; ++i) {
      const int n = socket_.receiveFrom(packet, sizeof(packet), from);
      if (n <= 0) break;
      receive(packet, static_cast<std::size_t>(n));
    }
  }
  tick();
}

void RemoteGamepad::tick() {
  bool any = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = clock_();
    for (Slot& slot : slots_) {
      if (slot.id && now - slot.heardAt >= kTimeoutMs) slot = Slot{};
      any = any || slot.id;
    }
  }
  if (!any && socket_.isOpen()) socket_.close();
}

std::vector<RemoteGamepad::Session> RemoteGamepad::sessions() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Session> out;
  for (int i = 0; i < kGamepadPlayers; ++i)
    if (slots_[i].id) out.push_back({slots_[i].id, i + 1, slots_[i].name});
  return out;
}

bool RemoteGamepad::controls(int player, HidControls& out) const {
  if (!validPlayer(player)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  const Slot& slot = slots_[player - 1];
  if (!slot.id) return false;
  out = slot.controls;
  return true;
}

std::string RemoteGamepad::name(int player) const {
  if (!validPlayer(player)) return {};
  std::lock_guard<std::mutex> lock(mutex_);
  return slots_[player - 1].name;
}

PlayerInput GamepadMux::input(int player) const {
  PlayerInput phone{GamepadStatus::Ready, {}};
  if (remote_.controls(player, phone.controls)) return phone;
  return bluetooth_ ? bluetooth_->input(player) : PlayerInput{};
}

std::string GamepadMux::name(int player) const {
  HidControls ignored;
  if (remote_.controls(player, ignored)) return remote_.name(player);
  return bluetooth_ ? bluetooth_->name(player) : std::string();
}

}
