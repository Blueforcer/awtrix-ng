#include "platform/posix/Bytes.h"
#include "platform/tc002/daemon/wifi/WpaControl.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>

namespace awtrix {
namespace tc002d {
namespace wifi {
namespace {

bool startsWith(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

bool fillAddress(const std::string& path, sockaddr_un& address) {
  std::memset(&address, 0, sizeof address);
  address.sun_family = AF_UNIX;
  if (path.empty() || path.size() >= sizeof address.sun_path) return false;
  std::memcpy(address.sun_path, path.data(), path.size());
  return true;
}

bool validUtf8WithoutControls(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    const uint8_t lead = static_cast<uint8_t>(text[i]);
    if (lead < 0x80) {
      if (lead < 0x20 || lead == 0x7f) return false;
      ++i;
      continue;
    }
    std::size_t length;
    uint32_t point;
    if ((lead & 0xe0) == 0xc0) { length = 2; point = lead & 0x1f; }
    else if ((lead & 0xf0) == 0xe0) { length = 3; point = lead & 0x0f; }
    else if ((lead & 0xf8) == 0xf0) { length = 4; point = lead & 0x07; }
    else return false;
    if (i + length > text.size()) return false;
    for (std::size_t j = 1; j < length; ++j) {
      const uint8_t next = static_cast<uint8_t>(text[i + j]);
      if ((next & 0xc0) != 0x80) return false;
      point = point << 6 | (next & 0x3f);
    }
    const uint32_t minimum = length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
    if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff) ||
        (point >= 0x80 && point < 0xa0))
      return false;
    i += length;
  }
  return true;
}

}

bool parseStatus(std::string_view reply, WpaStatus& out) {
  WpaStatus status;
  bool haveState = false;
  while (!reply.empty()) {
    const std::size_t end = reply.find('\n');
    const std::string_view line = reply.substr(0, end);
    reply = end == std::string_view::npos ? std::string_view() : reply.substr(end + 1);
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos) continue;
    const std::string_view key = line.substr(0, equals), value = line.substr(equals + 1);
    if (key == "wpa_state") { status.wpaState.assign(value.data(), value.size()); haveState = true; }
    else if (key == "ssid") status.ssid = decodePrintfEncoded(value);
    else if (key == "address") status.address.assign(value.data(), value.size());
    else if (key == "mode") status.mode.assign(value.data(), value.size());
  }
  if (!haveState || status.wpaState.empty()) return false;
  out = std::move(status);
  return true;
}

bool parseSignalPoll(std::string_view reply, int& rssi) {
  while (!reply.empty()) {
    const std::size_t end = reply.find('\n');
    const std::string_view line = reply.substr(0, end);
    reply = end == std::string_view::npos ? std::string_view() : reply.substr(end + 1);
    if (!startsWith(line, "RSSI=")) continue;
    std::string_view digits = line.substr(5);
    const bool negative = !digits.empty() && digits[0] == '-';
    if (negative) digits.remove_prefix(1);
    if (digits.empty() || digits.size() > 4) return false;
    int value = 0;
    for (char c : digits) {
      if (c < '0' || c > '9') return false;
      value = value * 10 + (c - '0');
    }
    rssi = negative ? -value : value;
    return true;
  }
  return false;
}

WpaEvent parseEvent(std::string_view message) {
  if (startsWith(message, "IFNAME=")) {
    const std::size_t space = message.find(' ');
    message = space == std::string_view::npos ? std::string_view() : message.substr(space + 1);
  }
  if (startsWith(message, "<")) {
    const std::size_t close = message.find('>');
    message = close == std::string_view::npos ? std::string_view() : message.substr(close + 1);
  }
  WpaEvent event;
  if (startsWith(message, "CTRL-EVENT-CONNECTED")) {
    event.type = WpaEventType::Connected;
  } else if (startsWith(message, "CTRL-EVENT-DISCONNECTED")) {
    event.type = WpaEventType::Disconnected;
  } else if (startsWith(message, "CTRL-EVENT-SSID-TEMP-DISABLED")) {
    event.type = WpaEventType::TemporarilyDisabled;
    const std::size_t reason = message.rfind(" reason=");
    if (reason != std::string_view::npos) {
      std::string_view value = message.substr(reason + 8);
      value = value.substr(0, value.find_first_of(" \r\n"));
      event.wrongKey = value == "WRONG_KEY";
    }
  } else if (startsWith(message, "CTRL-EVENT-TERMINATING")) {
    event.type = WpaEventType::Terminating;
  } else if (startsWith(message, "CTRL-EVENT-SCAN-RESULTS")) {
    event.type = WpaEventType::ScanResults;
  } else if (startsWith(message, "AP-ENABLED")) {
    event.type = WpaEventType::AccessPointEnabled;
  } else if (startsWith(message, "AP-DISABLED")) {
    event.type = WpaEventType::AccessPointDisabled;
  } else if (startsWith(message, "AP-STA-CONNECTED")) {
    event.type = WpaEventType::StationConnected;
  } else if (startsWith(message, "AP-STA-DISCONNECTED")) {
    event.type = WpaEventType::StationDisconnected;
  }
  return event;
}

std::vector<tc002::WifiNetwork> parseScanResults(std::string_view reply) {
  std::vector<tc002::WifiNetwork> networks;
  while (!reply.empty()) {
    const std::size_t end = reply.find('\n');
    std::string_view line = reply.substr(0, end);
    reply = end == std::string_view::npos ? std::string_view() : reply.substr(end + 1);
    std::string_view fields[5];
    unsigned count = 0;
    while (count < 5) {
      const std::size_t tab = count < 4 ? line.find('\t') : std::string_view::npos;
      fields[count++] = line.substr(0, tab);
      if (tab == std::string_view::npos) break;
      line.remove_prefix(tab + 1);
    }
    if (count != 5 || fields[0].size() != 17) continue;
    std::string_view level = fields[2];
    const bool negative = !level.empty() && level[0] == '-';
    if (negative) level.remove_prefix(1);
    if (level.empty() || level.size() > 4 || level.find_first_not_of("0123456789") != std::string_view::npos) continue;
    int rssi = 0;
    for (char c : level) rssi = rssi * 10 + (c - '0');
    rssi = negative ? (rssi > 200 ? -200 : -rssi) : 0;
    const std::string ssid = decodePrintfEncoded(fields[4]);
    if (ssid.empty() || ssid.size() > tc002::kMaxSsidBytes || !validUtf8WithoutControls(ssid)) continue;
    const std::string_view flags = fields[3];
    const bool secure = flags.find("WPA") != std::string_view::npos || flags.find("RSN") != std::string_view::npos ||
                        flags.find("WEP") != std::string_view::npos || flags.find("SAE") != std::string_view::npos;
    auto same = std::find_if(networks.begin(), networks.end(),
                             [&](const tc002::WifiNetwork& known) { return known.ssid == ssid; });
    if (same == networks.end()) {
      tc002::WifiNetwork network;
      network.ssid = ssid;
      network.rssi = rssi;
      network.secure = secure;
      networks.push_back(std::move(network));
    } else if (rssi > same->rssi) {
      same->rssi = rssi;
      same->secure = secure;
    }
  }
  std::stable_sort(networks.begin(), networks.end(),
                   [](const tc002::WifiNetwork& a, const tc002::WifiNetwork& b) { return a.rssi > b.rssi; });
  if (networks.size() > tc002::kMaxWifiScanNetworks) networks.resize(tc002::kMaxWifiScanNetworks);
  return networks;
}

std::string decodePrintfEncoded(std::string_view text) {
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\\' || i + 1 >= text.size()) { out += text[i]; continue; }
    const char code = text[++i];
    switch (code) {
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      case 'e': out += '\033'; break;
      case 'x':
        if (i + 2 < text.size() && posix::hexValue(text[i + 1]) >= 0 && posix::hexValue(text[i + 2]) >= 0) {
          out += static_cast<char>(posix::hexValue(text[i + 1]) << 4 | posix::hexValue(text[i + 2]));
          i += 2;
        } else {
          out += "\\x";
        }
        break;
      default: out += code; break;
    }
  }
  return out;
}

std::string displaySsid(std::string_view raw) {
  if (validUtf8WithoutControls(raw)) return std::string(raw);
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (unsigned char c : raw) {
    if (c == '\\') out += "\\\\";
    else if (c >= 0x20 && c < 0x7f) out += static_cast<char>(c);
    else { out += "\\x"; out += digits[c >> 4]; out += digits[c & 15]; }
  }
  return out;
}

bool WpaControlSocket::open(const std::string& localPath, const std::string& serverPath, int& error) {
  close();
  sockaddr_un local, server;
  if (!fillAddress(localPath, local) || !fillAddress(serverPath, server)) { error = ENAMETOOLONG; return false; }
  const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) { error = errno; return false; }
  unlink(localPath.c_str());
  if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0) {
    error = errno;
    ::close(fd);
    return false;
  }
  if (connect(fd, reinterpret_cast<sockaddr*>(&server), sizeof server) != 0) {
    error = errno;
    ::close(fd);
    unlink(localPath.c_str());
    return false;
  }
  fd_ = fd;
  localPath_ = localPath;
  return true;
}

void WpaControlSocket::close() {
  if (fd_ >= 0) ::close(fd_);
  if (!localPath_.empty()) unlink(localPath_.c_str());
  fd_ = -1;
  localPath_.clear();
}

bool WpaControlSocket::send(std::string_view command) {
  if (fd_ < 0) return false;
  const ssize_t n = ::send(fd_, command.data(), command.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
  return n == static_cast<ssize_t>(command.size());
}

bool WpaControlSocket::receive(std::string& out) {
  if (fd_ < 0) return false;
  char buffer[8192];
  const ssize_t n = recv(fd_, buffer, sizeof buffer, MSG_DONTWAIT);
  if (n <= 0) return false;
  out.assign(buffer, static_cast<std::size_t>(n));
  return true;
}

}
}
}
