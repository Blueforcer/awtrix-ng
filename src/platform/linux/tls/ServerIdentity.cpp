#include "platform/linux/tls/ServerIdentity.h"

#include <openssl/asn1.h>
#include <openssl/err.h>

#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::tls {

ServerIdentity::ServerIdentity(std::vector<X509Ptr> chain, KeyPtr key)
    : chain_(std::move(chain)), key_(std::move(key)) {}

ServerIdentity::~ServerIdentity() = default;

std::shared_ptr<const ServerIdentity> ServerIdentity::fromPem(const std::string& certificates, const std::string& key,
                                                              std::time_t now, std::string& error) {
  auto chain = readCertificates(certificates);
  KeyPtr privateKey = readPrivateKey(key);
  if (chain.empty()) { error = "invalid certificate"; return nullptr; }
  if (!privateKey) { error = "invalid key"; return nullptr; }
  if (X509_check_private_key(chain.front().get(), privateKey.get()) != 1) {
    ERR_clear_error();
    error = "key does not match";
    return nullptr;
  }
  X509* leaf = chain.front().get();
  if (X509_cmp_time(X509_get0_notAfter(leaf), &now) <= 0 || X509_cmp_time(X509_get0_notBefore(leaf), &now) >= 0) {
    ERR_clear_error();
    error = "certificate not valid now";
    return nullptr;
  }
  std::shared_ptr<ServerIdentity> identity(new ServerIdentity(std::move(chain), std::move(privateKey)));
  // The server certificate's key size and signature digest must pass the policy's security level.
  ContextPtr probe(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
  const bool usable = probe && applyPolicy(*probe) &&
      SSL_CTX_use_cert_and_key(probe.get(), identity->certificate(), identity->key(), nullptr, 1) == 1;
  ERR_clear_error();
  if (!usable) { error = "certificate too weak"; return nullptr; }
  return identity;
}

bool ServerIdentity::use(SSL_CTX& context) const {
  bool installed = SSL_CTX_use_certificate(&context, certificate()) == 1 &&
                   SSL_CTX_use_PrivateKey(&context, key_.get()) == 1;
  for (size_t i = 1; installed && i < chain_.size(); ++i) {
    X509_up_ref(chain_[i].get());
    if (SSL_CTX_add_extra_chain_cert(&context, chain_[i].get()) != 1) {
      X509_free(chain_[i].get());
      installed = false;
    }
  }
  ERR_clear_error();
  return installed;
}

std::unique_ptr<httplib::Server> newTlsServer(const ServerIdentity& identity) {
  auto server = std::make_unique<httplib::SSLServer>([&identity](SSL_CTX& context) {
    return applyPolicy(context) && identity.use(context);
  });
  if (!server->is_valid()) return nullptr;
  // Keep unauthenticated TLS/header work and waiting connections bounded.
  server->new_task_queue = [] { return new httplib::ThreadPool(8, 32); };
  server->set_keep_alive_timeout(3);
  server->set_keep_alive_max_count(32);
  return server;
}

}
