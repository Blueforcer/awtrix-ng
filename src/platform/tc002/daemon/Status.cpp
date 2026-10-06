#include "platform/tc002/daemon/Status.h"
#include "platform/posix/Time.h"

#include <unistd.h>

#include <ctime>

#include "platform/tc002/daemon/Log.h"

namespace awtrix {
namespace tc002d {

void appendDeviceState(api::JsonWriter& json, const DeviceState& state) {
  const auto& power = state.power();
  const auto& network = state.network();
  json.key("power").beginObject()
      .member("usbPower", power.usbPower)
      .member("batteryPercent", power.batteryPercent)
      .member("batteryMillivolts", power.batteryMillivolts)
      .endObject();
  json.key("network").beginObject()
      .member("link", tc002::wifiLinkName(network.link))
      .member("ssid", network.ssid)
      .member("rssi", network.rssi)
      .member("mac", network.mac)
      .member("ipv4", network.ipv4)
      .member("gateway", network.gateway)
      .member("dns", network.dns)
      .member("hostname", network.hostname)
      .endObject();
  json.key("time").beginObject().member("synchronized", state.time().synchronized).endObject();
}

std::string daemonStatus(const DaemonInfo& info, const DeviceState& state, const HardwareLease* lease,
                         const McuService* mcu, const RuntimeChild* runtime, int64_t nowMs,
                         const UpdateService* update, const AutostartService* autostart) {
  std::string out;
  api::JsonWriter json(out);
  json.beginObject();
  json.key("daemon").beginObject()
      .member("version", info.version)
      .member("pid", static_cast<int>(::getpid()))
      .member("uptimeMs", static_cast<long long>(nowMs - info.startedAtMs))
      .member("keepAdbTcp", info.keepAdbTcp)
      .endObject();
  appendDeviceState(json, state);
  if (lease) lease->appendStatus(json);
  if (mcu) mcu->appendStatus(json, nowMs);
  if (runtime) runtime->appendStatus(json, nowMs);
  if (update) update->appendStatus(json);
  if (autostart) autostart->appendStatus(json, nowMs);
  json.endObject();
  return out;
}

std::string okReply() { return "{\"ok\":true}"; }

void StateLog::power() {
  const auto& p = state_.power();
  if (powerKnown_ && p.usbPower == usb_ && p.batteryPercent == percent_) return;
  powerKnown_ = true;
  usb_ = p.usbPower;
  percent_ = p.batteryPercent;
  ++lines_;
  Log::line("state", "power usb=%d battery=%d%% %dmV", p.usbPower ? 1 : 0, p.batteryPercent, p.batteryMillivolts);
}

void StateLog::network(int64_t nowMs) {
  const auto& n = state_.network();
  if (networkKnown_ && n.link == network_.link && n.ssid == network_.ssid && n.ipv4 == network_.ipv4 &&
      n.gateway == network_.gateway && n.dns == network_.dns && n.hostname == network_.hostname)
    return;
  const bool ssidChanged = !networkKnown_ || n.ssid != network_.ssid;
  networkKnown_ = true;
  network_ = n;
  if (nowMs - windowStart_ >= kNetworkWindowMs) {
    if (suppressed_) Log::line("state", "%u network changes were not logged", suppressed_);
    windowStart_ = nowMs;
    windowLines_ = suppressed_ = 0;
  }
  if (windowLines_ >= kNetworkBurst) {
    if (suppressed_++ == 0) Log::line("state", "network changing often; logging paused for this window");
    return;
  }
  ++windowLines_;
  ++lines_;
  std::string ssid = ssidChanged ? " ssid=\"" + n.ssid + "\"" : std::string();
  Log::line("state", "network %s%s ip=%s gw=%s dns=%s host=%s", tc002::wifiLinkName(n.link), ssid.c_str(),
            n.ipv4.c_str(), n.gateway.c_str(), n.dns.c_str(), n.hostname.c_str());
}

void StateLog::time() {
  const bool synchronized = state_.time().synchronized;
  if (timeKnown_ && synchronized == synchronized_) return;
  timeKnown_ = true;
  synchronized_ = synchronized;
  const std::string wall = posix::wallClock();
  ++lines_;
  Log::line("state", "time %s, wall clock %s", synchronized ? "synchronized" : "not synchronized", wall.c_str());
}

LogReminder::LogReminder(const char* name, std::string message, int64_t intervalMs)
    : name_(name), message_(std::move(message)), intervalMs_(intervalMs > 0 ? intervalMs : 3600000) {}

bool LogReminder::start(int64_t nowMs) {
  emit(nowMs);
  return true;
}

void LogReminder::onTime(int64_t nowMs) {
  if (next_ >= 0 && nowMs >= next_) emit(nowMs);
}

void LogReminder::requestStop(int64_t nowMs) {
  (void)nowMs;
  next_ = -1;
}

void LogReminder::emit(int64_t nowMs) {
  ++lines_;
  Log::text(name_, message_);
  next_ = nowMs + intervalMs_;
}

}
}
