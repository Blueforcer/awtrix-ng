#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "platform/tc002/contract/SupervisorProtocol.h"

namespace awtrix {
namespace tc002d {
namespace wifi {

struct WpaStatus {
  std::string wpaState;
  std::string ssid;
  std::string address;
  std::string mode;
};

enum class WpaEventType { Other, Connected, Disconnected, TemporarilyDisabled, Terminating, ScanResults,
  AccessPointEnabled, AccessPointDisabled, StationConnected, StationDisconnected };

struct WpaEvent {
  WpaEventType type = WpaEventType::Other;
  bool wrongKey = false;
};

bool parseStatus(std::string_view reply, WpaStatus& out);
bool parseSignalPoll(std::string_view reply, int& rssi);
WpaEvent parseEvent(std::string_view message);
// SCAN_RESULTS reduced to what a network picker needs: SSIDs that are valid UTF-8 without control
// characters, one entry per SSID with its strongest signal, strongest first, at most 32.
std::vector<tc002::WifiNetwork> parseScanResults(std::string_view reply);
// Inverse of wpa_supplicant's printf_encode(), used for the ssid= field of STATUS.
std::string decodePrintfEncoded(std::string_view text);
// Valid UTF-8 without control characters passes unchanged; anything else is \xNN-escaped.
std::string displaySsid(std::string_view raw);

// One non-blocking datagram client of a wpa_supplicant control interface. The client end is
// bound to a path of our own so the supplicant can address replies and events to it.
class WpaControlSocket {
 public:
  WpaControlSocket() = default;
  WpaControlSocket(const WpaControlSocket&) = delete;
  WpaControlSocket& operator=(const WpaControlSocket&) = delete;
  ~WpaControlSocket() { close(); }

  bool open(const std::string& localPath, const std::string& serverPath, int& error);
  void close();
  bool isOpen() const { return fd_ >= 0; }
  int fd() const { return fd_; }
  bool send(std::string_view command);
  bool receive(std::string& out);

 private:
  int fd_ = -1;
  std::string localPath_;
};

}
}
}
