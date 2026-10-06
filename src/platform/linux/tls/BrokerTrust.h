#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "platform/linux/tls/TlsTrust.h"

namespace awtrix::tls {

// How the LAN service's MQTT client trusts its broker: a chain to the public roots, or the
// certificate the user pinned. An uploaded CA (/tls/mqtt-ca.pem) replaces both. A stored CA that
// cannot be read back trusts no broker at all rather than falling back to the public roots. Main
// loop only; the transport checks against peer().
class BrokerTrust {
 public:
  enum class Ca { Public, Uploaded, Unusable };
  // The largest CA file accepted on upload and read back at start.
  static constexpr std::size_t kMaxCaBytes = 64 * 1024;

  explicit BrokerTrust(const std::string& pin);
  std::shared_ptr<PeerTrust> peer() const { return peer_; }
  void setPin(const std::string& pin) { peer_->setPin(pin); }
  bool uploadCa(const std::string& pem, std::string& error);
  bool removeCa();
  Ca ca() const { return ca_; }

 private:
  std::shared_ptr<PeerTrust> peer_;
  Ca ca_ = Ca::Public;
};

}
