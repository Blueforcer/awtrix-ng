#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "platform/tc002/contract/RuntimeContract.h"
#include "platform/tc002/contract/WifiCredentials.h"
#include "platform/tc002/contract/MicrophoneStream.h"

// Messages between awtrix-tc002d and its awtrix-linux child. One JSON object per
// SOCK_SEQPACKET datagram on descriptor kSupervisorFd, at most kMaxSupervisorMessage bytes.
namespace awtrix {
namespace tc002 {

constexpr std::size_t kMaxSupervisorMessage = 4096;
constexpr std::size_t kMaxWifiScanNetworks = 32;
constexpr int kSupervisorProtocolVersion = 2;
constexpr std::size_t kMaxLogComponent = 16;
constexpr std::size_t kMaxLogText = 200;

struct PowerStatus {
  bool usbPower = false;
  int batteryPercent = -1;
  int batteryMillivolts = -1;
};

enum class WifiLink : uint8_t { Unconfigured, Connecting, Connected, Disconnected, Failed, AccessPoint };

struct NetworkStatus {
  WifiLink link = WifiLink::Unconfigured;
  std::string ssid;
  int rssi = 0;
  std::string mac;
  std::string ipv4;
  std::string gateway;
  std::string dns;
  std::string hostname;
};

// A visualization window: 44 DMA halves at nominal 16 kHz, mono signed PCM16.
// This is a requested snapshot, not a gapless recording or a stored audio file.
constexpr unsigned kMicrophonePcmHalves = 44;
constexpr unsigned kMicrophonePcmSamples = kMicrophonePcmHalves * 24;
constexpr unsigned kMicrophonePcmRate = 16000;
struct MicrophonePcm {
  int id = 0;
  std::vector<int16_t> samples;
  std::string error;
};

struct TimeStatus {
  bool synchronized = false;
};

// The fixed IPv4 setup the runtime wants on wlan0; enabled false means DHCP. The supervisor
// validates the dotted quads; gateway and the DNS servers may be empty.
struct StaticAddress {
  bool enabled = false;
  std::string ip, subnet, gateway, dns1, dns2;
};
inline bool operator==(const StaticAddress& a, const StaticAddress& b) {
  return a.enabled == b.enabled && a.ip == b.ip && a.subnet == b.subnet && a.gateway == b.gateway &&
         a.dns1 == b.dns1 && a.dns2 == b.dns2;
}
inline bool operator!=(const StaticAddress& a, const StaticAddress& b) { return !(a == b); }

// The runtime asks for the Bluetooth controller (on) or gives it back; the supervisor answers
// with the same message saying whether the controller is attached, and why not.
struct BluetoothStatus {
  bool on = false;
  std::string error;
};
constexpr std::size_t kMaxBluetoothError = 120;

struct WifiNetwork {
  std::string ssid;
  int rssi = 0;
  bool secure = false;
};

// Sent by awtrix-tc002d in its hello: the last web update, and how large a release image the
// release slot takes (0 while it is not known).
struct UpdateStatus {
  std::string state;
  std::string release;
  std::string error;
  std::uint64_t capacity = 0;
};

// Sent once by the runtime when its HTTP listener is bound and its first frame is on the display.
struct RuntimeReady {
  std::string board;
  int width = 0;
  int height = 0;
  bool input = false;
};

// A line of the awtrix-tc002d log that the runtime adds to its own (/api/v1/logs): a component of
// lowercase letters and hyphens and at most kMaxLogText bytes of text.
struct LogLine {
  std::string component;
  std::string text;
};

// The runtime verified a package and hands it to the supervisor for installation.
struct UpdateReady {
  std::string package;
  std::string release;
  std::uint64_t counter = 0;
  std::string payloadSha256;
};

enum class MessageType : uint8_t {
  Invalid,
  Hello,
  Power,
  Network,
  Time,
  Wifi,
  Ntp,
  Hostname,
  Address,
  Reboot,
  FactoryReset,
  WifiScan,
  WifiScanResult,
  UpdateReady,
  Ready,
  Log,
  Bluetooth,
  MicrophonePcmRequest,
  MicrophonePcm,
  MicrophoneStreamControl,
  MicrophoneStreamEvent,
};

struct SupervisorMessage {
  MessageType type = MessageType::Invalid;
  std::string version;
  PowerStatus power;
  NetworkStatus network;
  TimeStatus time;
  MicrophonePcm microphonePcm;
  StreamControl streamControl;
  StreamEvent streamEvent;
  BluetoothStatus bluetooth;
  WifiCredentials wifi;
  std::string ntpServer;
  std::string hostname;
  StaticAddress address;
  std::vector<WifiNetwork> networks;
  bool hasUpdate = false;
  UpdateStatus update;
  UpdateReady updateReady;
  RuntimeReady ready;
  LogLine log;
};

const char* wifiLinkName(WifiLink link);

std::string encodeHello(std::string_view version);
// The supervisor's hello carries the update status; the runtime's does not.
std::string encodeHello(std::string_view version, const UpdateStatus& update);
std::string encodePower(const PowerStatus& power);
std::string encodeNetwork(const NetworkStatus& network);
std::string encodeTime(const TimeStatus& time);
std::string encodeMicrophonePcmRequest(int id);
std::string encodeMicrophonePcm(const MicrophonePcm& pcm);
std::string encodeBluetooth(const BluetoothStatus& bluetooth);
std::string encodeWifi(const WifiCredentials& wifi);
std::string encodeNtp(std::string_view server);
std::string encodeHostname(std::string_view hostname);
std::string encodeAddress(const StaticAddress& address);
std::string encodeReboot();
std::string encodeFactoryReset();
std::string encodeWifiScan();
// Keeps the leading valid networks, at most kMaxWifiScanNetworks and as many as fit one datagram.
std::string encodeWifiScanResult(const std::vector<WifiNetwork>& networks);
std::string encodeUpdateReady(const UpdateReady& ready);
std::string encodeReady(const RuntimeReady& ready);
// Cuts the text to kMaxLogText bytes at a character boundary; empty for an invalid component.
std::string encodeLog(const LogLine& line);

bool decodeSupervisorMessage(std::string_view datagram, SupervisorMessage& out);

}
}
