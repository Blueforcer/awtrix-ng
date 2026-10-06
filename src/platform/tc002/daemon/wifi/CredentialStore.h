#pragma once

#include <string>
#include <string_view>

#include "platform/tc002/contract/WifiCredentials.h"
#include "platform/tc002/daemon/wifi/WifiCrypto.h"

namespace awtrix {
namespace tc002d {
namespace wifi {

using tc002::kMaxSsidBytes;

// What reaches the disk and the supplicant: SSID bytes and the derived PSK, never the passphrase.
struct WifiProfile {
  WifiProfile() = default;
  WifiProfile(const WifiProfile&) = default;
  WifiProfile& operator=(const WifiProfile&) = default;
  ~WifiProfile() { secureErase(psk.data(), psk.size()); }

  std::string ssid;
  bool open = false;
  WpaPsk psk{};
};

enum class StoreStatus { Loaded, Missing, Invalid, Unsafe, IoError };

const char* storeStatusName(StoreStatus status);

// Validates the user input and derives the profile; false leaves `out` untouched.
bool makeProfile(std::string_view ssid, std::string_view password, WifiProfile& out);

class CredentialStore {
 public:
  explicit CredentialStore(std::string directory);

  StoreStatus load(WifiProfile& out) const;
  bool save(const WifiProfile& profile, int& error) const;
  // Unlinks the record (a logical delete: JFFS2 does not scrub the old nodes).
  bool erase(int& error) const;

  const std::string& directory() const { return directory_; }

  static std::string encode(const WifiProfile& profile);
  static bool decode(std::string_view record, WifiProfile& out);

  static constexpr const char* kFileName = "wifi.cred";
  static constexpr const char* kTemporaryName = "wifi.cred.tmp";

 private:
  int openDirectory(bool create, int& error) const;
  std::string directory_;
};

}
}
}
