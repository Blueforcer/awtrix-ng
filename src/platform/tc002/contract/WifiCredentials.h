#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// The credentials the runtime hands to awtrix-tc002d, and the one rule both sides and the Wi-Fi
// service check them against.
namespace awtrix {
namespace tc002 {

constexpr std::size_t kMaxSsidBytes = 32;

struct WifiCredentials {
  std::string ssid;
  std::string password;
};

// 8 to 63 characters without control characters.
inline bool wifiPassphrase(std::string_view password) {
  if (password.size() < 8 || password.size() > 63) return false;
  for (const unsigned char c : password)
    if (c < 0x20 || c == 0x7f) return false;
  return true;
}

// 64 hex digits: the pre-shared key itself.
inline bool wifiHexKey(std::string_view password) {
  if (password.size() != 64) return false;
  for (const char c : password)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  return true;
}

enum class WifiCredentialsProblem { None, Ssid, Password };

// An SSID of 1 to 32 bytes; an empty password (an open network), a passphrase or a hex key.
inline WifiCredentialsProblem checkWifiCredentials(std::string_view ssid, std::string_view password) {
  if (ssid.empty() || ssid.size() > kMaxSsidBytes) return WifiCredentialsProblem::Ssid;
  if (!password.empty() && !wifiPassphrase(password) && !wifiHexKey(password))
    return WifiCredentialsProblem::Password;
  return WifiCredentialsProblem::None;
}

inline bool validWifiCredentials(const WifiCredentials& credentials) {
  return checkWifiCredentials(credentials.ssid, credentials.password) == WifiCredentialsProblem::None;
}

}
}
