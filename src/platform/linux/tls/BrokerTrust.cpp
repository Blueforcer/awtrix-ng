#include "platform/linux/tls/BrokerTrust.h"

#include <filesystem>
#include <system_error>

#include "platform/linux/host/HostStore.h"
#include "system/Log.h"

namespace awtrix::tls {
namespace {
const char* const kUploadedCa = "/tls/mqtt-ca.pem";
}

BrokerTrust::BrokerTrust(const std::string& pin) : peer_(std::make_shared<PeerTrust>(publicAnchors(), true)) {
  peer_->setPin(pin);
  const std::string path = host::hostPath(kUploadedCa);
  std::error_code error;
  if (std::filesystem::symlink_status(path, error).type() == std::filesystem::file_type::not_found) return;
  std::string pem;
  if (auto anchors = host::readFile(path, pem, kMaxCaBytes) ? TrustAnchors::fromPem(pem) : nullptr) {
    peer_->setAnchors(std::move(anchors), false);
    ca_ = Ca::Uploaded;
    return;
  }
  peer_->setAnchors(nullptr, false);
  ca_ = Ca::Unusable;
  logf("mqtt: uploaded CA unusable, broker refused");
}

bool BrokerTrust::uploadCa(const std::string& pem, std::string& error) {
  if (pem.size() > kMaxCaBytes) { error = "at most " + std::to_string(kMaxCaBytes) + " bytes"; return false; }
  auto anchors = TrustAnchors::fromPem(pem);
  if (!anchors) { error = "invalid certificate"; return false; }
  std::error_code ignored;
  std::filesystem::create_directory(host::hostPath("/tls"), ignored);
  if (!host::writeFile(host::hostPath(kUploadedCa), pem)) { error = "not saved"; return false; }
  peer_->setAnchors(std::move(anchors), false);
  ca_ = Ca::Uploaded;
  return true;
}

bool BrokerTrust::removeCa() {
  std::error_code error;
  std::filesystem::remove(host::hostPath(kUploadedCa), error);
  if (error) return false;
  peer_->setAnchors(publicAnchors(), true);
  ca_ = Ca::Public;
  return true;
}

}
