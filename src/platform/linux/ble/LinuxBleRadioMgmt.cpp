#include "platform/linux/ble/LinuxBleRadio.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "platform/linux/ble/LinuxBleProtocol.h"

namespace awtrix::ble {
using namespace linux_detail;

int LinuxBleRadio::mgmt(uint16_t op, const Bytes& params, Bytes* reply) {
  return management_.command(op, index_, params, reply);
}

void LinuxBleRadio::readMgmt() {
  management_.readEvents();
  Bytes event;
  while (management_.nextEvent(event)) mgmtEvent(event.data(), event.size());
}

void LinuxBleRadio::mgmtEvent(const uint8_t* p, std::size_t n) {
  if (n < 6) return;
  const uint16_t event = le16(p);
  const uint16_t index = le16(p + 2);
  const uint8_t* d = p + 6;
  const std::size_t len = n - 6;
  auto reply = [&](uint16_t op) {
    Bytes packet;
    put16(packet, op);
    put16(packet, index_);
    put16(packet, 7);
    packet.insert(packet.end(), d, d + 7);
    management_.write(packet);
  };
  switch (event) {
    case kIndexAdded:
      if (state_ == State::Waiting) {
        index_ = index;
        configure();
      }
      break;
    case kIndexRemoved:
      if (state_ != State::Off && index == index_) fail("the Bluetooth controller went away");
      break;
    case kNewLtk:
      if (len >= 1 + BondStore::kLtkInfo && d[0])
        bonds_.rememberLtk(Bytes(d + 1, d + 1 + BondStore::kLtkInfo));
      break;
    case kNewIrk:
      if (len >= 1 + 6 + BondStore::kIrkInfo && d[0])
        bonds_.rememberIrk(Bytes(d + 7, d + 7 + BondStore::kIrkInfo));
      break;
    case kUserConfirmRequest:
      if (len >= 7) reply(kUserConfirmReply);
      break;
    case kUserPasskeyRequest:
      if (len >= 7) reply(kUserPasskeyNegativeReply);
      break;
    case kDeviceDisconnected:
      if (len >= 8) {
        static const char* const kWhy[] = {"unknown", "timeout", "closed here", "closed by the peer", "authentication failed"};
        note("ble: " + fromWire(d, d[6]).str() + " disconnected (" + (d[7] < 5 ? kWhy[d[7]] : "reason " + std::to_string(d[7])) + ")");
      }
      break;
    case kAuthFailed:
      if (len >= 8) {
        const Address who = fromWire(d, d[6]);
        for (auto& [id, link] : links_) {
          if (link.peer.b != who.b || !link.secureTarget) continue;
          link.secureTarget = 0;
          link.secureDeadline = -1;
          if (events_.secured) events_.secured(id, false, "pairing failed (status " + std::to_string(d[7]) + ")");
        }
      }
      break;
    default:
      break;
  }
}

void LinuxBleRadio::power(bool on) {
  if (!on) {
    shutdown();
    if (events_.power) events_.power(Power::Off, "");
    return;
  }
  if (state_ != State::Off) return;
  if (!management_.open()) {
    fail(std::string("no Bluetooth support: ") + std::strerror(errno));
    return;
  }
  Bytes list;
  if (mgmt(kReadIndexList, {}, &list) == 0 && list.size() >= 4 && le16(list.data()) > 0) {
    index_ = le16(list.data() + 2);
    state_ = State::Waiting;
    configure();
    return;
  }
  if (!options_.requestController) {
    fail("no Bluetooth controller");
    return;
  }
  state_ = State::Waiting;
  waitDeadline_ = now_ + options_.attachTimeoutMs;
  options_.requestController(true);
}

void LinuxBleRadio::controllerStatus(bool on, const std::string& error) {
  if (!on && state_ == State::Waiting && !error.empty()) fail(error);
}

void LinuxBleRadio::configure() {
  waitDeadline_ = -1;
  Bytes info;
  if (mgmt(kReadInfo, {}, &info) != 0 || info.size() < 6) {
    fail("the Bluetooth controller does not answer");
    return;
  }
  address_ = fromWire(info.data(), kLePublic);
  Bytes features;
  slots_ = mgmt(kReadAdvFeatures, {}, &features) == 0 && features.size() >= 7 ? features[6] : 0;
  mgmt(kSetIoCapability, {0x03});
  mgmt(kSetBondable, {0x01});
  mgmt(kSetConnectable, {0x01});
  Bytes irks;
  put16(irks, static_cast<uint16_t>(bonds_.irks().size()));
  for (const Bytes& k : bonds_.irks()) irks.insert(irks.end(), k.begin(), k.end());
  if (mgmt(kLoadIrks, irks) != 0) note("ble: the controller refused the stored identity keys");
  Bytes ltks;
  put16(ltks, static_cast<uint16_t>(bonds_.ltks().size()));
  for (const Bytes& k : bonds_.ltks()) ltks.insert(ltks.end(), k.begin(), k.end());
  if (mgmt(kLoadLtks, ltks) != 0) note("ble: the controller refused the stored pairing keys");
  setName(name_);
  if (mgmt(kSetPowered, {0x01}) != 0) {
    fail("the Bluetooth controller does not switch on");
    return;
  }
  if (!openScanner() || !openListener()) {
    fail(std::string("cannot open Bluetooth sockets: ") + std::strerror(errno));
    return;
  }
  state_ = State::On;
  note("ble: controller on as " + address_.str() + ", " + std::to_string(slots_) + " advert slots, " +
       std::to_string(bonds_.ltks().size()) + " bonds");
  if (events_.power) events_.power(Power::On, "");
}

void LinuxBleRadio::shutdown() {
  // Nothing is wanted any more, so closing the links resumes neither scanning nor advertising.
  scanWanted_ = false;
  adverts_.clear();
  std::vector<int> ids;
  for (const auto& [id, link] : links_) ids.push_back(id);
  for (int id : ids) close(id, false);
  if (scanFd_ >= 0) ::close(scanFd_);
  if (listenFd_ >= 0) ::close(listenFd_);
  scanFd_ = listenFd_ = -1;
  const bool wasUp = state_ != State::Off;
  if (state_ == State::On) mgmt(kSetPowered, {0x00});
  state_ = State::Off;
  directLink_ = 0;
  cancel_.reset();
  peripheralHandles_.clear();
  hciErrors_.clear();
  waitDeadline_ = -1;
  if (wasUp && options_.requestController) options_.requestController(false);
}

void LinuxBleRadio::fail(const std::string& why) {
  note("ble: " + why);
  shutdown();
  if (events_.power) events_.power(Power::Failed, why);
}

void LinuxBleRadio::setName(const std::string& name) {
  name_ = name.substr(0, 248);
  if (management_.fd() < 0 || state_ == State::Off) return;
  Bytes params(249 + 11, 0);
  std::copy(name_.begin(), name_.end(), params.begin());
  std::copy(name_.begin(), name_.begin() + static_cast<long>(std::min<std::size_t>(name_.size(), 10)), params.begin() + 249);
  mgmt(kSetLocalName, params);
}

}  // namespace awtrix::ble
