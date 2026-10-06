#include "platform/tc002/daemon/wifi/WifiScan.h"

#include <utility>

#include "core/api/JsonWriter.h"

namespace awtrix::tc002d::wifi {
namespace {
constexpr int64_t kEventMs = 10000;
constexpr int64_t kGiveUpMs = 20000;
constexpr std::size_t kMaxControlReply = 4000;
}

bool WifiScan::start(int64_t now) {
  if (active_) return false;
  error_.clear();
  active_ = true;
  resetControl();
  giveUpAt_ = now + kGiveUpMs;
  return true;
}

void WifiScan::wait(Done done) {
  if (done) waiters_.push_back(std::move(done));
}

void WifiScan::finish(std::vector<tc002::WifiNetwork> networks, bool fresh, int64_t now) {
  if (fresh) {
    networks_ = std::move(networks);
    lastAt_ = now;
  }
  active_ = false;
  resetControl();
  giveUpAt_ = -1;
  std::vector<Done> waiters;
  waiters.swap(waiters_);
  for (auto& done : waiters)
    if (done) done(networks_);
}

void WifiScan::resetControl() {
  sent_ = wantResults_ = false;
  eventDeadline_ = -1;
}

void WifiScan::commandReply(std::string_view reply, int64_t now) {
  if (!active_) return;
  sent_ = true;
  if (reply.compare(0, 2, "OK") == 0 || reply.compare(0, 9, "FAIL-BUSY") == 0)
    eventDeadline_ = now + kEventMs;
  else
    wantResults_ = true;
}

void WifiScan::resultsEvent(bool allowUnrequested) {
  if (active_ && sent_) {
    eventDeadline_ = -1;
    wantResults_ = true;
  } else if (!active_ && allowUnrequested) {
    wantResults_ = true;
  }
}

bool WifiScan::timedOut(int64_t now) const {
  return active_ && giveUpAt_ >= 0 && now >= giveUpAt_;
}

void WifiScan::onTime(int64_t now) {
  if (active_ && eventDeadline_ >= 0 && now >= eventDeadline_) {
    eventDeadline_ = -1;
    wantResults_ = true;
  }
}

int64_t WifiScan::nextDeadlineMs() const {
  if (!active_) return -1;
  if (eventDeadline_ >= 0 && (giveUpAt_ < 0 || eventDeadline_ < giveUpAt_)) return eventDeadline_;
  return giveUpAt_;
}

bool WifiScan::contains(std::string_view ssid) const {
  for (const auto& network : networks_)
    if (network.ssid == ssid) return true;
  return false;
}

std::string WifiScan::json(int64_t now) const {
  std::size_t count = networks_.size();
  for (;;) {
    std::string out;
    api::JsonWriter json(out);
    json.beginObject().member("scanning", active_).member("error", error_)
        .member("ageMs", lastAt_ < 0 ? int64_t(-1) : now - lastAt_);
    json.key("networks").beginArray();
    for (std::size_t i = 0; i < count; ++i)
      json.beginObject().member("ssid", networks_[i].ssid)
          .member("rssi", networks_[i].rssi).member("secure", networks_[i].secure).endObject();
    json.endArray().endObject();
    if (out.size() <= kMaxControlReply || count == 0) return out;
    --count;
  }
}

}  // namespace awtrix::tc002d::wifi
