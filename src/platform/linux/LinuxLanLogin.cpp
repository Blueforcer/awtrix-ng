#include "platform/linux/Auth.h"
#include "core/payload/Base64.h"
#include "platform/linux/LinuxLanLogin.h"

#include <openssl/crypto.h>
#include <openssl/sha.h>

#include "core/api/ApiRouter.h"
#include "persistence/DeviceConfig.h"
#include "platform/linux/host/vendor/httplib.h"

namespace awtrix {
namespace {
std::string credentialsOf(const std::string& authorization) {
  const auto first = authorization.find_first_not_of(" \t", 6);
  const auto last = authorization.find_last_not_of(" \t");
  return first == std::string::npos ? std::string() : authorization.substr(first, last - first + 1);
}
}

void LinuxLanLogin::update(const DeviceConfig& config) {
  std::string pair = config.authUser + ":" + config.authPass;
  std::string encoded = base64::encode(pair.data(), pair.size());
  const auth::Digest expected = auth::digest(encoded);
  auth::wipe(pair);
  auth::wipe(encoded);
  std::lock_guard<std::mutex> lock(mutex_);
  required_ = config.authEnabled;
  expected_ = expected;
}

bool LinuxLanLogin::admit(const httplib::Request& request, httplib::Response& response) const {
  if (request.method == "OPTIONS") return true;
  auth::Digest expected{};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!required_) return true;
    expected = expected_;
  }
  if (request.get_header_value_count("Authorization") == 1) {
    const std::string authorization = request.get_header_value("Authorization");
    if (authorization.rfind("Basic ", 0) == 0) {
      std::string credentials = credentialsOf(authorization);
      const auth::Digest candidate = auth::digest(credentials);
      auth::wipe(credentials);
      if (auth::equal(candidate, expected)) return true;
    }
  }
  return auth::unauthorized(response, api::kAuthenticationChallenge);
}

}
