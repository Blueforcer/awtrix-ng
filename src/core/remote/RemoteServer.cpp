#include "core/remote/RemoteServer.h"

#include <algorithm>

#include "core/render/Fingerprint.h"

namespace awtrix {
namespace remote {

namespace {
bool sameApps(const std::vector<HostApp>& a, const std::vector<HostApp>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i].name != b[i].name || a[i].flags != b[i].flags || a[i].durationMs != b[i].durationMs) return false;
  return true;
}
}

void RemoteServer::receive(const net::Endpoint& from, const uint8_t* data, std::size_t length,
                           int64_t nowMs) {
  wire::Packet packet;
  if (!wire::decode(data, length, packet)) return;
  if (packet.type == wire::Type::Hello) {
    hello(packet, from, nowMs);
    return;
  }
  if (connected_ && from == endpoint_ && packet.type == wire::Type::Show) show(packet.show, nowMs);
}

void RemoteServer::hello(const wire::Packet& packet, const net::Endpoint& from, int64_t nowMs) {
  const wire::Hello& h = packet.hello;
  if (h.uid != uid_ || !verify_ || !verify_(packet)) return;
  if (connected_ && static_cast<int32_t>(h.counter - counter_) <= 0) return;
  const bool fresh = !connected_ || from != endpoint_;
  counter_ = h.counter;
  endpoint_ = from;
  connected_ = true;
  lastHelloMs_ = nowMs;
  clock_.counter = h.counter;
  clock_.width = h.width;
  clock_.height = h.height;
  clock_.address = h.address;
  clock_.webPort = h.webPort;
  clock_.state = h.state;
  clock_.sensors = h.sensors;
  clock_.settingsRevision = h.settingsRevision;
  clock_.systemRevision = h.systemRevision;
  clock_.assetsRevision = h.assetsRevision;
  if (fresh) switchesKnown_ = false;
  if (listed_ && h.appsRevision == appsRevision_) takeOffMask(h.offMask);
  if (!fresh) return;
  ++connection_;
  forgetShow();
  sent_.clear();
  if (listed_) sendApps(nowMs);
}

void RemoteServer::takeOffMask(uint32_t mask) {
  switchesKnown_ = true;
  switchedOff_.clear();
  for (std::size_t i = 0; i < apps_.size(); ++i)
    if (mask & (1u << i)) switchedOff_.push_back(apps_[i].name);
}

void RemoteServer::show(const wire::Show& show, int64_t nowMs) {
  if (haveShow_ && static_cast<int16_t>(show.sequence - sequence_) < 0) return;
  for (int i = 0; i < wire::kButtons; ++i) {
    if (haveShow_) buttons_[i].pending += static_cast<uint8_t>(show.presses[i] - presses_[i]);
    presses_[i] = show.presses[i];
  }
  haveShow_ = true;
  sequence_ = show.sequence;
  lastShowMs_ = nowMs;
  down_ = show.buttonsDown;
  if (show.current == current_ && show.incoming == incoming_) return;
  current_.assign(show.current.data(), show.current.size());
  incoming_.assign(show.incoming.data(), show.incoming.size());
  for (auto it = sent_.begin(); it != sent_.end();)
    it = (it->first == current_ || it->first == incoming_) ? std::next(it) : sent_.erase(it);
}

void RemoteServer::forgetShow() {
  haveShow_ = false;
  current_.clear();
  incoming_.clear();
  down_ = 0;
  for (Button& button : buttons_) button = Button{};
}

void RemoteServer::stepButtons(bool (&pressed)[wire::kButtons]) {
  for (int i = 0; i < wire::kButtons; ++i) {
    Button& b = buttons_[i];
    const bool down = (down_ >> i) & 1u;
    if (b.inPress && !down) {
      b.inPress = false;
      pressed[i] = false;
      continue;
    }
    if (!b.inPress && b.pending > 0) {
      --b.pending;
      b.inPress = true;
    }
    pressed[i] = b.inPress;
  }
}

void RemoteServer::setApps(std::vector<HostApp> apps, int64_t nowMs) {
  apps.erase(std::remove_if(apps.begin(), apps.end(),
                            [](const HostApp& app) { return app.name.empty() || app.name.size() > wire::kMaxName; }),
             apps.end());
  if (apps.size() > wire::kMaxApps) apps.resize(wire::kMaxApps);
  if (listed_ && sameApps(apps, apps_)) return;
  apps_ = std::move(apps);
  listed_ = true;
  switchesKnown_ = false;
  ++appsRevision_;
  if (connected_) sendApps(nowMs);
}

void RemoteServer::sendApps(int64_t nowMs) {
  wire::Apps list;
  list.revision = appsRevision_;
  for (const HostApp& app : apps_) {
    wire::AppEntry& entry = list.entries[list.count++];
    entry.name = app.name;
    entry.flags = app.flags;
    entry.durationMs = app.durationMs;
  }
  uint8_t packet[wire::kMaxDatagram];
  const std::size_t length = wire::encodeApps(list, packet, sizeof(packet));
  if (length) sink_.send(endpoint_, packet, length);
  lastAppsMs_ = nowMs;
}

void RemoteServer::tick(int64_t nowMs) {
  if (connected_ && nowMs - lastHelloMs_ > kClockSilenceMs) {
    connected_ = false;
    forgetShow();
  }
  if (haveShow_ && nowMs - lastShowMs_ > kShowSilenceMs) forgetShow();
  if (connected_ && listed_ && nowMs - lastAppsMs_ >= kAppsEveryMs) sendApps(nowMs);
}

void RemoteServer::sendFrame(const std::string& app, const Canvas& canvas, int64_t nowMs) {
  if (!connected_ || app.empty() || canvas.width() != clock_.width || canvas.height() != clock_.height)
    return;
  Fingerprint print;
  print.add(canvas);
  Sent& sent = sent_[app];
  if (sent.atMs != 0 && sent.fingerprint == print.value() && nowMs - sent.atMs < kKeepaliveMs) return;
  const int step = wire::rowsPerDatagram(canvas.width(), app);
  if (step <= 0) return;
  const uint16_t stamp = static_cast<uint16_t>(nowMs);
  uint8_t packet[wire::kMaxDatagram];
  for (int row = 0; row < canvas.height(); row += step) {
    const int rows = std::min(step, canvas.height() - row);
    const std::size_t length = wire::encodeFrame(canvas, app, stamp, row, rows, packet, sizeof(packet));
    if (length) sink_.send(endpoint_, packet, length);
  }
  sent.fingerprint = print.value();
  sent.atMs = nowMs == 0 ? 1 : nowMs;
}

}
}
