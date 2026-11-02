#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "platform/linux/tls/TlsTrust.h"

namespace awtrix::tls {

// Host-scoped trust shared by MQTT, Voice and scripts. Only explicit administration writes it;
// network workers may record a refused leaf for the user's decision, never trust it themselves.
class TrustStore {
 public:
  static constexpr std::size_t kMaxCaBytes = 64 * 1024, kMaxHosts = 32, kMaxPending = 32;
  explicit TrustStore(std::string directory);
  static std::string normalizeHost(const std::string& host);
  PeerTrust::Verdict check(ssl_st* ssl, const std::string& host, bool* customTrust = nullptr);
  bool custom(const std::string& host) const;
  std::string pending(const std::string& host) const;
  std::string status() const;
  // Returns an HTTP status; failure leaves both saved and active trust unchanged.
  int uploadCa(const std::string& host, const std::string& pem);
  int approve(const std::string& host, const std::string& fingerprint);
  int remove(const std::string& host, const std::string& part = {});
  bool reset();

 private:
  struct Entry {
    std::string pin, ca;
    std::shared_ptr<const TrustAnchors> anchors;
  };
  using Entries = std::map<std::string, Entry>;
  bool load();
  bool save(const Entries& entries);
  std::string directory_;
  mutable std::mutex mutex_;
  Entries entries_;
  std::map<std::string, std::string> pending_;
  bool usable_ = false;
};

// Each HTTP hop receives its own destination policy; automatic redirects are disabled.
void useHostTlsTrust(httplib::Client& client, std::shared_ptr<TrustStore> store, const std::string& host, bool* customTrust = nullptr);

}
