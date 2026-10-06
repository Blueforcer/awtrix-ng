#include "platform/posix/Files.h"
#include "platform/linux/tls/TlsPolicy.h"

#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

namespace awtrix::tls {
namespace {
using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;

int noPassword(char*, int, int, void*) { return 0; }

BioPtr readBio(const std::string& text) {
  return BioPtr(BIO_new_mem_buf(text.data(), static_cast<int>(text.size())), BIO_free);
}

}

bool applyProtocolPolicy(SSL_CTX& context) {
  SSL_CTX_set_options(&context, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION);
  return SSL_CTX_set_min_proto_version(&context, TLS1_2_VERSION) == 1;
}

bool applyPolicy(SSL_CTX& context) {
  SSL_CTX_set_security_level(&context, 2);
  return applyProtocolPolicy(context) &&
         SSL_CTX_set_cipher_list(&context, "ECDHE+AESGCM:ECDHE+CHACHA20") == 1;
}

ContextPtr newClientContext() {
  ContextPtr context(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
  if (!context || !applyPolicy(*context)) return ContextPtr(nullptr, SSL_CTX_free);
  SSL_CTX_set_verify(context.get(), SSL_VERIFY_NONE, nullptr);
  SSL_CTX_set_mode(context.get(), SSL_MODE_ENABLE_PARTIAL_WRITE);
  return context;
}

std::string fingerprint(X509* certificate) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  if (!certificate || X509_digest(certificate, EVP_sha256(), digest, &length) != 1) return "";
  return posix::hexBytes(digest, length);
}

std::vector<X509Ptr> readCertificates(const std::string& pem) {
  std::vector<X509Ptr> certificates;
  BioPtr input = readBio(pem);
  if (!input) return certificates;
  while (X509* certificate = PEM_read_bio_X509(input.get(), nullptr, noPassword, nullptr))
    certificates.emplace_back(certificate, X509_free);
  ERR_clear_error();
  return certificates;
}

KeyPtr readPrivateKey(const std::string& pem) {
  BioPtr input = readBio(pem);
  KeyPtr key(input ? PEM_read_bio_PrivateKey(input.get(), nullptr, noPassword, nullptr) : nullptr,
             EVP_PKEY_free);
  ERR_clear_error();
  return key;
}

bool isIpLiteral(const std::string& name) {
  unsigned char address[16];
  return inet_pton(AF_INET, name.c_str(), address) == 1 || inet_pton(AF_INET6, name.c_str(), address) == 1;
}

bool certificateNames(X509* certificate, const std::string& name) {
  if (!certificate || name.empty() || name.find('\0') != std::string::npos) return false;
  const bool named = isIpLiteral(name)
      ? X509_check_ip_asc(certificate, name.c_str(), 0) == 1
      : X509_check_host(certificate, name.c_str(), name.size(), X509_CHECK_FLAG_NEVER_CHECK_SUBJECT, nullptr) == 1;
  ERR_clear_error();
  return named;
}

}
