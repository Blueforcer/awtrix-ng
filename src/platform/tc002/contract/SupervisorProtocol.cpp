#include "core/Sha256Hex.h"
#include "core/render/TextEncoding.h"
#include "platform/tc002/contract/SupervisorProtocol.h"

#include <cstring>
#include <limits>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/tc002/contract/Pcm16.h"
#include "platform/tc002/contract/ReleaseName.h"

namespace awtrix {
namespace tc002 {
namespace {

constexpr std::size_t kMaxField = 256;
constexpr std::size_t kMaxUpdateState = 32;

bool readPcm(const api::JsonReader& root, MicrophonePcm& pcm) {
  const auto field = [&](const char* name) { return api::memberValue(root, name); };
  std::string encoded;
  if (!field("error").isString() || !field("error").appendString(pcm.error) || pcm.error.size() > 120 ||
      pcm.error.find('\0') != std::string::npos || !field("pcm").isString() || !field("pcm").appendString(encoded)) return false;
  if (!pcm.error.empty()) return encoded.empty();
  if (encoded.size() != kMicrophonePcmSamples * 8 / 3) return false;
  return decodePcm16(encoded, pcm.samples) && pcm.samples.size() == kMicrophonePcmSamples;
}

void begin(api::JsonWriter& json, const char* type) {
  json.beginObject().member("v", kSupervisorProtocolVersion).member("type", type);
}

std::string finish(api::JsonWriter& json, std::string& out) {
  json.endObject();
  return out.size() <= kMaxSupervisorMessage ? out : std::string();
}

bool readString(const api::JsonReader& value, std::string& out) {
  if (!value.isString()) return false;
  std::string text;
  if (!value.appendString(text) || text.size() > kMaxField) return false;
  out = std::move(text);
  return true;
}

bool readInt(const api::JsonReader& value, int low, int high, int& out) {
  long long n = 0;
  if (!value.isInteger() || !value.asLong(n) || n < low || n > high) return false;
  out = static_cast<int>(n);
  return true;
}

bool readBounded(const api::JsonReader& value, std::size_t limit, std::string& out) {
  std::string text;
  if (!readString(value, text) || text.size() > limit || text.find('\0') != std::string::npos) return false;
  out = std::move(text);
  return true;
}

bool updateStateName(const std::string& value) {
  if (value.empty() || value.size() > kMaxUpdateState) return false;
  for (const char c : value)
    if (!((c >= 'a' && c <= 'z') || c == '-')) return false;
  return true;
}

bool validUpdate(const UpdateStatus& update) {
  return updateStateName(update.state) && update.release.size() <= kMaxReleaseNameBytes &&
         update.error.size() <= kMaxField && update.release.find('\0') == std::string::npos &&
         update.error.find('\0') == std::string::npos;
}

bool validUpdateReady(const UpdateReady& ready) {
  return !ready.package.empty() && ready.package.front() == '/' && ready.package.size() <= kMaxField &&
         ready.package.find('\0') == std::string::npos && validReleaseName(ready.release) && ready.counter > 0 &&
         ready.counter <= static_cast<std::uint64_t>(std::numeric_limits<long long>::max()) &&
         isSha256Hex(ready.payloadSha256);
}

bool readUpdate(api::JsonReader value, UpdateStatus& out) {
  UpdateStatus update;
  long long capacity = 0;
  const api::JsonReader room = api::memberValue(value, "capacity");
  if (!value.isObject() || !readBounded(api::memberValue(value, "state"), kMaxUpdateState, update.state) ||
      !readBounded(api::memberValue(value, "release"), kMaxReleaseNameBytes, update.release) ||
      !readBounded(api::memberValue(value, "error"), kMaxField, update.error) || !validUpdate(update) ||
      !room.isInteger() || !room.asLong(capacity) || capacity < 0)
    return false;
  update.capacity = static_cast<std::uint64_t>(capacity);
  out = std::move(update);
  return true;
}

bool readAddress(const api::JsonReader& root, StaticAddress& out) {
  StaticAddress address;
  const auto field = [&](const char* name) { return api::memberValue(root, name); };
  if (!field("static").asBool(address.enabled) || !readString(field("ip"), address.ip) ||
      !readString(field("subnet"), address.subnet) || !readString(field("gateway"), address.gateway) ||
      !readString(field("dns1"), address.dns1) || !readString(field("dns2"), address.dns2))
    return false;
  out = std::move(address);
  return true;
}

bool readUpdateReady(const api::JsonReader& root, UpdateReady& out) {
  UpdateReady ready;
  long long counter = 0;
  const api::JsonReader count = api::memberValue(root, "counter");
  if (!readBounded(api::memberValue(root, "package"), kMaxField, ready.package) ||
      !readBounded(api::memberValue(root, "release"), kMaxReleaseNameBytes, ready.release) ||
      !count.isInteger() || !count.asLong(counter) || counter < 1 ||
      !readBounded(api::memberValue(root, "payloadSha256"), 64, ready.payloadSha256))
    return false;
  ready.counter = static_cast<std::uint64_t>(counter);
  if (!validUpdateReady(ready)) return false;
  out = std::move(ready);
  return true;
}

bool readLink(const api::JsonReader& value, WifiLink& out) {
  std::string name;
  if (!readString(value, name)) return false;
  for (WifiLink link : {WifiLink::Unconfigured, WifiLink::Connecting, WifiLink::Connected,
                        WifiLink::Disconnected, WifiLink::Failed, WifiLink::AccessPoint}) {
    if (name == wifiLinkName(link)) { out = link; return true; }
  }
  return false;
}

bool validReady(const RuntimeReady& ready) {
  return (ready.board == "tc002" || ready.board == "headless") && ready.width >= 8 && ready.width <= 128 &&
         ready.height >= 8 && ready.height <= 32 && (!ready.input || ready.board == "tc002");
}

MessageType typeFromName(std::string_view name) {
  if (name == "hello") return MessageType::Hello;
  if (name == "power") return MessageType::Power;
  if (name == "network") return MessageType::Network;
  if (name == "time") return MessageType::Time;
  if (name == "wifi") return MessageType::Wifi;
  if (name == "ntp") return MessageType::Ntp;
  if (name == "hostname") return MessageType::Hostname;
  if (name == "address") return MessageType::Address;
  if (name == "reboot") return MessageType::Reboot;
  if (name == "factoryReset") return MessageType::FactoryReset;
  if (name == "wifiScan") return MessageType::WifiScan;
  if (name == "wifiScanResult") return MessageType::WifiScanResult;
  if (name == "updateReady") return MessageType::UpdateReady;
  if (name == "microphonePcmRequest") return MessageType::MicrophonePcmRequest;
  if (name == "microphonePcm") return MessageType::MicrophonePcm;
  if (name == "microphoneStreamControl") return MessageType::MicrophoneStreamControl;
  if (name == "microphoneStreamEvent") return MessageType::MicrophoneStreamEvent;
  if (name == "ready") return MessageType::Ready;
  if (name == "log") return MessageType::Log;
  if (name == "bluetooth") return MessageType::Bluetooth;
  return MessageType::Invalid;
}

bool validLogComponent(std::string_view component) {
  if (component.empty() || component.size() > kMaxLogComponent) return false;
  for (const char c : component)
    if ((c < 'a' || c > 'z') && c != '-') return false;
  return true;
}

bool validNetwork(const WifiNetwork& network) {
  return network.ssid.size() <= kMaxSsidBytes && network.rssi >= -200 && network.rssi <= 0;
}

bool readNetworks(api::JsonReader list, std::vector<WifiNetwork>& out) {
  if (!list.isArray() || !list.enterArray()) return false;
  while (list.nextElement()) {
    WifiNetwork network;
    if (out.size() == kMaxWifiScanNetworks || !list.isObject() ||
        !readString(api::memberValue(list, "ssid"), network.ssid) ||
        !readInt(api::memberValue(list, "rssi"), -200, 0, network.rssi) ||
        !api::memberValue(list, "secure").asBool(network.secure) || !validNetwork(network))
      return false;
    out.push_back(std::move(network));
    if (!list.skipValue()) return false;
  }
  return list.ok();
}

}

const char* wifiLinkName(WifiLink link) {
  switch (link) {
    case WifiLink::Unconfigured: return "unconfigured";
    case WifiLink::Connecting: return "connecting";
    case WifiLink::Connected: return "connected";
    case WifiLink::Disconnected: return "disconnected";
    case WifiLink::Failed: return "failed";
    case WifiLink::AccessPoint: return "access-point";
  }
  return "unconfigured";
}

std::string encodeHello(std::string_view version) {
  std::string out;
  api::JsonWriter json(out);
  begin(json, "hello");
  json.member("version", version);
  return finish(json, out);
}

std::string encodeHello(std::string_view version, const UpdateStatus& update) {
  if (!validUpdate(update) ||
      update.capacity > static_cast<std::uint64_t>(std::numeric_limits<long long>::max()))
    return std::string();
  std::string out;
  api::JsonWriter json(out);
  begin(json, "hello");
  json.member("version", version);
  json.key("update").beginObject().member("state", update.state).member("release", update.release)
      .member("error", update.error).member("capacity", static_cast<long long>(update.capacity)).endObject();
  return finish(json, out);
}

std::string encodePower(const PowerStatus& power) {
  std::string out;
  api::JsonWriter json(out);
  begin(json, "power");
  json.member("usbPower", power.usbPower)
      .member("batteryPercent", power.batteryPercent)
      .member("batteryMillivolts", power.batteryMillivolts);
  return finish(json, out);
}

std::string encodeNetwork(const NetworkStatus& network) {
  std::string out;
  api::JsonWriter json(out);
  begin(json, "network");
  json.member("link", wifiLinkName(network.link))
      .member("ssid", network.ssid)
      .member("rssi", network.rssi)
      .member("mac", network.mac)
      .member("ipv4", network.ipv4)
      .member("gateway", network.gateway)
      .member("dns", network.dns)
      .member("hostname", network.hostname);
  return finish(json, out);
}

std::string encodeBluetooth(const BluetoothStatus& bluetooth) {
  if (bluetooth.error.size() > kMaxBluetoothError) return std::string();
  std::string out;
  api::JsonWriter json(out);
  begin(json, "bluetooth");
  json.member("on", bluetooth.on).member("error", bluetooth.error);
  return finish(json, out);
}

std::string encodeTime(const TimeStatus& time) {
  std::string out;
  api::JsonWriter json(out);
  begin(json, "time");
  json.member("synchronized", time.synchronized);
  return finish(json, out);
}

std::string encodeWifi(const WifiCredentials& wifi) {
  std::string out;
  api::JsonWriter json(out);
  begin(json, "wifi");
  json.member("ssid", wifi.ssid).member("password", wifi.password);
  return finish(json, out);
}

std::string encodeNtp(std::string_view server) {
  std::string out;
  api::JsonWriter json(out);
  begin(json, "ntp");
  json.member("server", server);
  return finish(json, out);
}

std::string encodeHostname(std::string_view hostname) {
  std::string out;
  api::JsonWriter json(out);
  begin(json, "hostname");
  json.member("name", hostname);
  return finish(json, out);
}

std::string encodeAddress(const StaticAddress& address) {
  std::string out;
  api::JsonWriter json(out);
  begin(json, "address");
  json.member("static", address.enabled)
      .member("ip", address.ip)
      .member("subnet", address.subnet)
      .member("gateway", address.gateway)
      .member("dns1", address.dns1)
      .member("dns2", address.dns2);
  return finish(json, out);
}

std::string encodeLog(const LogLine& line) {
  if (!validLogComponent(line.component)) return std::string();
  const auto clipped = text::clipBytes(line.text, kMaxLogText);
  std::string out;
  api::JsonWriter json(out);
  begin(json, "log");
  json.member("component", line.component).member("text", clipped);
  return finish(json, out);
}

std::string encodeReboot() {
  std::string out;
  api::JsonWriter json(out);
  begin(json, "reboot");
  return finish(json, out);
}

std::string encodeFactoryReset() {
  std::string out;
  api::JsonWriter json(out);
  begin(json, "factoryReset");
  return finish(json, out);
}

std::string encodeWifiScan() {
  std::string out;
  api::JsonWriter json(out);
  begin(json, "wifiScan");
  return finish(json, out);
}

std::string encodeWifiScanResult(const std::vector<WifiNetwork>& networks) {
  std::vector<const WifiNetwork*> valid;
  for (const auto& network : networks)
    if (valid.size() < kMaxWifiScanNetworks && validNetwork(network)) valid.push_back(&network);
  for (;;) {
    std::string out;
    api::JsonWriter json(out);
    begin(json, "wifiScanResult");
    json.key("networks").beginArray();
    for (const WifiNetwork* network : valid)
      json.beginObject().member("ssid", network->ssid).member("rssi", network->rssi)
          .member("secure", network->secure).endObject();
    json.endArray();
    json.endObject();
    if (out.size() <= kMaxSupervisorMessage) return out;
    if (valid.empty()) return std::string();
    valid.pop_back();
  }
}

std::string encodeUpdateReady(const UpdateReady& ready) {
  if (!validUpdateReady(ready)) return std::string();
  std::string out;
  api::JsonWriter json(out);
  begin(json, "updateReady");
  json.member("package", ready.package)
      .member("release", ready.release)
      .member("counter", static_cast<unsigned long long>(ready.counter))
      .member("payloadSha256", ready.payloadSha256);
  return finish(json, out);
}

std::string encodeReady(const RuntimeReady& ready) {
  if (!validReady(ready)) return std::string();
  std::string out;
  api::JsonWriter json(out);
  begin(json, "ready");
  json.member("board", ready.board).member("width", ready.width).member("height", ready.height)
      .member("input", ready.input);
  return finish(json, out);
}

std::string encodeMicrophonePcmRequest(int id) {
  if (id <= 0) return {};
  std::string out;
  api::JsonWriter json(out);
  begin(json, "microphonePcmRequest");
  json.member("id", id);
  return finish(json, out);
}

std::string encodeMicrophonePcm(const MicrophonePcm& pcm) {
  if (pcm.id <= 0 || pcm.error.size() > 120 || pcm.error.find('\0') != std::string::npos ||
      (pcm.error.empty() ? pcm.samples.size() != kMicrophonePcmSamples : !pcm.samples.empty())) return {};
  std::string out;
  api::JsonWriter json(out);
  begin(json, "microphonePcm");
  json.member("id", pcm.id).member("error", pcm.error).member("pcm", encodePcm16(pcm.samples));
  return finish(json, out);
}

bool decodeSupervisorMessage(std::string_view datagram, SupervisorMessage& out) {
  out = SupervisorMessage{};
  if (datagram.empty() || datagram.size() > kMaxSupervisorMessage) return false;
  api::JsonReader root(datagram);
  if (!root.isObject()) return false;
  int version = 0;
  if (!readInt(api::memberValue(root, "v"), 0, 1000, version) || version != kSupervisorProtocolVersion)
    return false;
  std::string typeName;
  if (!readString(api::memberValue(root, "type"), typeName)) return false;
  SupervisorMessage message;
  message.type = typeFromName(typeName);
  const auto field = [&](const char* name) { return api::memberValue(root, name); };
  bool ok = true;
  switch (message.type) {
    case MessageType::Invalid:
      return false;
    case MessageType::Hello: {
      ok = readString(field("version"), message.version);
      const api::JsonReader update = field("update");
      if (ok && api::present(update)) ok = message.hasUpdate = readUpdate(update, message.update);
      break;
    }
    case MessageType::Power: {
      auto& p = message.power;
      ok = field("usbPower").asBool(p.usbPower) &&
           readInt(field("batteryPercent"), -1, 100, p.batteryPercent) &&
           readInt(field("batteryMillivolts"), -1, 10000, p.batteryMillivolts);
      break;
    }
    case MessageType::Network: {
      auto& n = message.network;
      ok = readLink(field("link"), n.link) && readString(field("ssid"), n.ssid) &&
           readInt(field("rssi"), -200, 0, n.rssi) && readString(field("mac"), n.mac) &&
           readString(field("ipv4"), n.ipv4) && readString(field("gateway"), n.gateway) &&
           readString(field("dns"), n.dns) && readString(field("hostname"), n.hostname);
      break;
    }
    case MessageType::Time:
      ok = field("synchronized").asBool(message.time.synchronized);
      break;
    case MessageType::MicrophonePcmRequest:
      ok = readInt(field("id"), 1, std::numeric_limits<int>::max(), message.microphonePcm.id);
      break;
    case MessageType::MicrophoneStreamControl:
      ok = decodeStreamControl(datagram, message.streamControl);
      break;
    case MessageType::MicrophoneStreamEvent:
      ok = decodeStreamEvent(datagram, message.streamEvent);
      break;
    case MessageType::MicrophonePcm:
      ok = readInt(field("id"), 1, std::numeric_limits<int>::max(), message.microphonePcm.id) &&
           readPcm(root, message.microphonePcm);
      break;
    case MessageType::Bluetooth:
      ok = field("on").asBool(message.bluetooth.on) &&
           readBounded(field("error"), kMaxBluetoothError, message.bluetooth.error);
      break;
    case MessageType::Wifi:
      ok = readString(field("ssid"), message.wifi.ssid) &&
           readString(field("password"), message.wifi.password) && validWifiCredentials(message.wifi);
      break;
    case MessageType::Ntp:
      ok = readString(field("server"), message.ntpServer);
      break;
    case MessageType::Hostname:
      ok = readString(field("name"), message.hostname);
      break;
    case MessageType::Address:
      ok = readAddress(root, message.address);
      break;
    case MessageType::Reboot:
    case MessageType::FactoryReset:
    case MessageType::WifiScan:
      break;
    case MessageType::WifiScanResult:
      ok = readNetworks(field("networks"), message.networks);
      break;
    case MessageType::UpdateReady:
      ok = readUpdateReady(root, message.updateReady);
      break;
    case MessageType::Log:
      ok = readBounded(field("component"), kMaxLogComponent, message.log.component) &&
           validLogComponent(message.log.component) && readBounded(field("text"), kMaxLogText, message.log.text);
      break;
    case MessageType::Ready:
      ok = readBounded(field("board"), 16, message.ready.board) && readInt(field("width"), 8, 128, message.ready.width) &&
           readInt(field("height"), 8, 32, message.ready.height) && field("input").asBool(message.ready.input) &&
           validReady(message.ready);
      break;
  }
  if (!ok) return false;
  out = std::move(message);
  return true;
}

}
}
