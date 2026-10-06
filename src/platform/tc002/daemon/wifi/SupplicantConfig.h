#pragma once

#include <string>
#include <string_view>

#include "platform/tc002/daemon/wifi/CredentialStore.h"

namespace awtrix {
namespace tc002d {
namespace wifi {

// Private wpa_supplicant configuration: our control directory, no config writes, exactly one
// network whose SSID and PSK are hex so no byte of user input is ever parsed as syntax. Only a
// hidden network is probed for by name. A PSK network also accepts WPA-PSK-SHA256 and optional
// PMF. Returns an empty string when the profile or the directory cannot be represented safely.
std::string supplicantConfig(const WifiProfile& profile, std::string_view controlDirectory, bool hidden);
// No network at all: only the control interface, used to scan before any credentials exist.
std::string scanOnlyConfig(std::string_view controlDirectory);
std::string accessPointConfig(std::string_view ssid, std::string_view controlDirectory);

// Writes `text` to `path` with mode 0600 through a temporary file and an atomic rename.
bool writePrivateFile(const std::string& path, const std::string& text, int& error);

}
}
}
