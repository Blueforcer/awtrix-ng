#pragma once

#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "platform/linux/tls/TlsPolicy.h"

namespace httplib { class Server; }

namespace awtrix::tls {

// A server certificate, its chain and private key, checked to belong together.
class ServerIdentity {
 public:
  // The first certificate is the server's, any further ones its chain up to the first block that
  // does not parse. Refuses a key that does not match, a certificate outside its validity at now
  // and one the policy's security level rejects.
  static std::shared_ptr<const ServerIdentity> fromPem(const std::string& certificates, const std::string& key,
                                                       std::time_t now, std::string& error);
  ~ServerIdentity();
  X509* certificate() const { return chain_.front().get(); }
  EVP_PKEY* key() const { return key_.get(); }
  bool names(const std::string& name) const { return certificateNames(certificate(), name); }
  // Installs certificate, chain and key on a server context.
  bool use(SSL_CTX& context) const;

 private:
  ServerIdentity(std::vector<X509Ptr> chain, KeyPtr key);
  std::vector<X509Ptr> chain_;
  KeyPtr key_;
};

// An HTTPS listener under the TLS policy that presents identity, with its worker threads and
// waiting connections bounded.
std::unique_ptr<httplib::Server> newTlsServer(const ServerIdentity& identity);

}
