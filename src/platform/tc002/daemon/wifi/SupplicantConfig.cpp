#include "platform/tc002/daemon/wifi/SupplicantConfig.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

#include "platform/posix/Files.h"

namespace awtrix {
namespace tc002d {
namespace wifi {
namespace {

bool safePath(std::string_view path) {
  if (path.size() < 2 || path.size() > 96 || path[0] != '/' || path.back() == '/') return false;
  for (char c : path) {
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                         c == '/' || c == '_' || c == '-' || c == '.';
    if (!allowed) return false;
  }
  return path.find("//") == std::string_view::npos && path.find("/../") == std::string_view::npos;
}

}


std::string supplicantConfig(const WifiProfile& profile, std::string_view controlDirectory, bool hidden) {
  if (!safePath(controlDirectory) || profile.ssid.empty() || profile.ssid.size() > kMaxSsidBytes) return {};
  std::string text = "ctrl_interface=";
  text.append(controlDirectory.data(), controlDirectory.size());
  text += "\nupdate_config=0\nnetwork={\n\tssid=";
  text += posix::hexBytes(profile.ssid.data(), profile.ssid.size());
  text += '\n';
  if (hidden) text += "\tscan_ssid=1\n";
  if (profile.open) {
    text += "\tkey_mgmt=NONE\n";
  } else {
    text += "\tkey_mgmt=WPA-PSK WPA-PSK-SHA256\n\tieee80211w=1\n\tpsk=";
    std::string psk = posix::hexBytes(profile.psk.data(), profile.psk.size());
    text += psk;
    secureErase(&psk[0], psk.size());
    text += '\n';
  }
  text += "}\n";
  return text;
}

std::string scanOnlyConfig(std::string_view controlDirectory) {
  if (!safePath(controlDirectory)) return {};
  std::string text = "ctrl_interface=";
  text.append(controlDirectory.data(), controlDirectory.size());
  text += "\nupdate_config=0\n";
  return text;
}

std::string accessPointConfig(std::string_view ssid, std::string_view controlDirectory) {
  if (ssid.empty() || ssid.size() > kMaxSsidBytes) return {};
  std::string text = scanOnlyConfig(controlDirectory);
  if (text.empty()) return {};
  text += "network={\n\tssid=" + posix::hexBytes(ssid.data(), ssid.size());
  text += "\n\tmode=2\n\tfrequency=2412\n\tkey_mgmt=NONE\n}\n";
  return text;
}

bool writePrivateFile(const std::string& path, const std::string& text, int& error) {
  const bool ok = posix::replaceText(path, text);
  error = ok ? 0 : errno;
  return ok;
}

}
}
}
