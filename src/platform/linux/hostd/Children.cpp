#include "platform/linux/hostd/Children.h"

#include <algorithm>
#include <cstdio>

#include "core/remote/PairingKey.h"
#include "core/remote/RemoteWire.h"
#include "platform/linux/remote/RunnerOptions.h"
#include "transport/net/HmacSha256.h"

namespace awtrix::hostd {

bool Children::launch(const ClockStore& store, const ClockRecord& clock, Child& child, int64_t nowMs) {
  const std::string data = store.dataDir(clock.uid).string();
  const std::string log = (store.clockDir(clock.uid) / "runtime.log").string();
  std::vector<std::string> args = {binary_, "--data", data, "--port", std::to_string(clock.port),
                                   "--width", std::to_string(clock.width), "--height", std::to_string(clock.height),
                                   "--clock", clock.uid, "--key-file", store.keyFile(clock.uid).string(),
                                   "--clock-url", clock.clockUrl};
  if (!webui_.empty()) {
    args.push_back("--webui");
    args.push_back(webui_);
  }
  if (!caFile_.empty()) {
    args.push_back("--ca-file");
    args.push_back(caFile_);
  }
  if (idleSeconds_ > 0) {
    args.push_back("--idle");
    args.push_back(std::to_string(idleSeconds_));
  }
  if (!child.process.start(args, log)) return false;
  child.startedMs = nowMs;
  std::printf("clock %s: process %ld on port %d\n", clock.uid.c_str(), child.process.id(), clock.port);
  std::fflush(stdout);
  return true;
}

bool Children::heard(const ClockRecord& clock, Child& child) {
  if (!child.door.valid()) child.door.reset(os::openUdp(static_cast<uint16_t>(clock.port)));
  if (!child.door.valid()) return false;
  remote::PairingKey key;
  const bool keyed = remote::PairingKey::parse(clock.keyHex, key);
  uint8_t data[remote::wire::kMaxDatagram];
  sockaddr_in from{};
  bool heard = false;
  for (long length; (length = os::receiveDatagram(child.door.get(), data, sizeof data, from)) > 0;) {
    remote::wire::Packet packet;
    heard = heard || (keyed && remote::wire::decode(data, static_cast<std::size_t>(length), packet) &&
                      packet.type == remote::wire::Type::Hello && packet.hello.uid == clock.uid &&
                      remote::signedBy(packet, key, hmacSha256));
  }
  return heard;
}

void Children::sync(const ClockStore& store, int64_t nowMs) {
  for (auto it = children_.begin(); it != children_.end();) {
    if (store.find(it->first)) {
      ++it;
      continue;
    }
    it->second.process.stop();
    it = children_.erase(it);
  }
  for (const ClockRecord& clock : store.clocks()) {
    Child& child = children_[clock.uid];
    if (!child.waiting && !child.process.running() && child.restartAtMs <= nowMs) launch(store, clock, child, nowMs);
  }
}

void Children::awaitAll(const ClockStore& store) {
  for (const ClockRecord& clock : store.clocks()) {
    Child& child = children_[clock.uid];
    if (!child.process.running()) child.waiting = true;
  }
}

void Children::restart(const std::string& uid) {
  const auto it = children_.find(uid);
  if (it == children_.end()) return;
  it->second.process.stop();
  it->second = Child{};
}

void Children::stop(const std::string& uid) {
  const auto it = children_.find(uid);
  if (it == children_.end()) return;
  it->second.process.stop();
  children_.erase(it);
}

void Children::stopAll() {
  for (auto& entry : children_) entry.second.process.stop();
  children_.clear();
}

void Children::tick(const ClockStore& store, int64_t nowMs) {
  for (auto& entry : children_) {
    Child& child = entry.second;
    const ClockRecord* clock = store.find(entry.first);
    if (child.process.running()) {
      const long pid = child.process.id();
      int status = 0;
      if (!child.process.ended(status)) continue;
      if (status == remote::kClockSilentExit) {
        child.waiting = true;
        child.backoffMs = kFirstBackoffMs;
        std::printf("clock %s: silent, its scripts wait for it\n", entry.first.c_str());
        std::fflush(stdout);
        continue;
      }
      const bool healthy = nowMs - child.startedMs >= kHealthyMs;
      child.backoffMs = healthy ? kFirstBackoffMs : std::min(child.backoffMs * 2, kMaxBackoffMs);
      child.restartAtMs = nowMs + (healthy ? kFirstBackoffMs : child.backoffMs);
      std::printf("clock %s: process %ld ended (status %d), again in %lld ms\n", entry.first.c_str(), pid, status,
                  static_cast<long long>(child.restartAtMs - nowMs));
      std::fflush(stdout);
      continue;
    }
    if (child.waiting) {
      if (!clock || !heard(*clock, child)) continue;
      child.waiting = false;
      child.door.reset();
      std::printf("clock %s: heard again\n", entry.first.c_str());
      std::fflush(stdout);
      launch(store, *clock, child, nowMs);
      continue;
    }
    if (nowMs < child.restartAtMs) continue;
    if (clock) launch(store, *clock, child, nowMs);
  }
}

bool Children::running(const std::string& uid) const {
  const auto it = children_.find(uid);
  return it != children_.end() && it->second.process.running();
}

bool Children::waiting(const std::string& uid) const {
  const auto it = children_.find(uid);
  return it != children_.end() && it->second.waiting;
}

}
