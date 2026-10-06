#pragma once

#include <memory>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace awtrix::tls {

using X509Ptr = std::unique_ptr<X509, decltype(&X509_free)>;
using KeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using ContextPtr = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;

// Protocol floor shared by public-trust and pinned/private-trust contexts.
bool applyProtocolPolicy(SSL_CTX& context);

// The one TLS policy of this runtime, for its servers and its MQTT client: TLS 1.2 or newer,
// ECDHE key exchange with AES-GCM or ChaCha20-Poly1305, security level 2, no compression or
// renegotiation.
bool applyPolicy(SSL_CTX& context);
// A client context under the policy. Peers are not verified during the handshake: PeerTrust
// decides afterwards, before anything is sent.
ContextPtr newClientContext();

// SHA-256 over the certificate's DER encoding, 64 lowercase hex digits.
std::string fingerprint(X509* certificate);

// The certificates of a PEM text in order, up to the first block that does not parse.
std::vector<X509Ptr> readCertificates(const std::string& pem);
KeyPtr readPrivateKey(const std::string& pem);

// A DNS name or an IPv4/IPv6 literal the certificate is issued for.
bool certificateNames(X509* certificate, const std::string& name);
bool isIpLiteral(const std::string& name);

}
