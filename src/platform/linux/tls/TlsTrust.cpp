#include "platform/linux/tls/TlsTrust.h"

#include <sys/resource.h>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/tls/TlsPolicy.h"
#include "platform/posix/StartThread.h"
#include "system/Log.h"

namespace awtrix::tls {
namespace {

std::mutex g_publicMutex;
std::string g_publicFile;
bool g_publicFileFailed = false;
std::shared_ptr<const TrustAnchors> g_public;
constexpr int kParseNice = 10;

// Half a second of a core on the TC002, which speech that just started needs more: its ring is barely
// filled. So the parsing yields, on a thread of its own, which also leaves the asker's priority alone.
std::shared_ptr<const TrustAnchors> parseInBackground(const std::string& path) {
  std::shared_ptr<const TrustAnchors> anchors;
  std::thread parser;
  if (!posix::startThread(parser, [&] {
        ::setpriority(PRIO_PROCESS, 0, kParseNice);
        anchors = TrustAnchors::fromFile(path);
      }))
    return TrustAnchors::fromFile(path);
  parser.join();
  return anchors;
}

bool expectName(X509_VERIFY_PARAM* parameters, const std::string& name) {
  if (name.empty() || name.find('\0') != std::string::npos) return false;
  if (isIpLiteral(name)) return X509_VERIFY_PARAM_set1_ip_asc(parameters, name.c_str()) == 1;
  X509_VERIFY_PARAM_set_hostflags(parameters, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS | X509_CHECK_FLAG_NEVER_CHECK_SUBJECT);
  return X509_VERIFY_PARAM_set1_host(parameters, name.c_str(), 0) == 1;
}

}

TrustAnchors::~TrustAnchors() { X509_STORE_free(store_); }

std::shared_ptr<const TrustAnchors> TrustAnchors::adopt(X509_STORE* store) {
  const bool filled = store && sk_X509_OBJECT_num(X509_STORE_get0_objects(store)) > 0;
  ERR_clear_error();
  if (!filled) {
    X509_STORE_free(store);
    return nullptr;
  }
  return std::shared_ptr<const TrustAnchors>(new TrustAnchors(store));
}

std::shared_ptr<const TrustAnchors> TrustAnchors::fromPem(const std::string& pem) {
  const auto certificates = readCertificates(pem);
  if (certificates.empty()) return nullptr;
  X509_STORE* store = X509_STORE_new();
  for (const auto& certificate : certificates) {
    if (!store || X509_STORE_add_cert(store, certificate.get()) != 1) {
      X509_STORE_free(store);
      ERR_clear_error();
      return nullptr;
    }
  }
  return adopt(store);
}

std::shared_ptr<const TrustAnchors> TrustAnchors::fromFile(const std::string& path) {
  X509_STORE* store = X509_STORE_new();
  if (store && X509_STORE_load_file(store, path.c_str()) != 1) {
    X509_STORE_free(store);
    store = nullptr;
  }
  return adopt(store);
}

void setPublicAnchors(std::shared_ptr<const TrustAnchors> anchors) {
  std::lock_guard<std::mutex> lock(g_publicMutex);
  g_publicFile.clear();
  g_public = std::move(anchors);
}

void setPublicAnchorsFile(const std::string& path) {
  std::lock_guard<std::mutex> lock(g_publicMutex);
  g_publicFile = path;
  g_publicFileFailed = false;
  g_public.reset();
}

bool publicAnchorsNamed() {
  std::lock_guard<std::mutex> lock(g_publicMutex);
  return g_public || !g_publicFile.empty();
}

std::shared_ptr<const TrustAnchors> publicAnchors() {
  std::lock_guard<std::mutex> lock(g_publicMutex);
  if (!g_public && !g_publicFile.empty()) {
    g_public = parseInBackground(g_publicFile);
    if (!g_public && !g_publicFileFailed)
      logf("tls: no certificates in %s; HTTPS connections will fail", g_publicFile.c_str());
    g_publicFileFailed = !g_public;
  }
  return g_public;
}

bool expectPeer(SSL* ssl, const std::string& host) {
  if (host.empty() || host.find('\0') != std::string::npos) return false;
  if (!isIpLiteral(host) && SSL_set_tlsext_host_name(ssl, host.c_str()) != 1) return false;
  return expectName(SSL_get0_param(ssl), host);
}

bool chainTrusted(const TrustAnchors* anchors, SSL* ssl, const std::string& host) {
  X509* leaf = SSL_get0_peer_certificate(ssl);
  if (!anchors || !leaf) return false;
  std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)> context(X509_STORE_CTX_new(),
                                                                          X509_STORE_CTX_free);
  const bool trusted = context &&
      X509_STORE_CTX_init(context.get(), anchors->store(), leaf, SSL_get_peer_cert_chain(ssl)) == 1 &&
      X509_STORE_CTX_set_default(context.get(), "ssl_server") == 1 &&
      expectName(X509_STORE_CTX_get0_param(context.get()), host) && X509_verify_cert(context.get()) == 1;
  ERR_clear_error();
  return trusted;
}

PeerTrust::PeerTrust(std::shared_ptr<const TrustAnchors> anchors, bool pinning)
    : anchors_(std::move(anchors)), pinning_(pinning) {}

PeerTrust::PeerTrust(PublicRoots, bool pinning) : publicRoots_(true), pinning_(pinning) {}

void PeerTrust::setAnchors(std::shared_ptr<const TrustAnchors> anchors, bool pinning) {
  std::lock_guard<std::mutex> lock(mutex_);
  anchors_ = std::move(anchors);
  publicRoots_ = false;
  pinning_ = pinning;
  pending_.clear();
}

void PeerTrust::usePublicRoots(bool pinning) {
  std::lock_guard<std::mutex> lock(mutex_);
  anchors_.reset();
  publicRoots_ = true;
  pinning_ = pinning;
  pending_.clear();
}

void PeerTrust::setPin(const std::string& fingerprint) {
  std::lock_guard<std::mutex> lock(mutex_);
  pin_ = fingerprint;
  if (pin_ == pending_) pending_.clear();
}

PeerTrust::Verdict PeerTrust::check(SSL* ssl, const std::string& host) {
  std::shared_ptr<const TrustAnchors> anchors;
  bool publicRoots;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    anchors = anchors_;
    publicRoots = publicRoots_;
  }
  if (publicRoots) anchors = publicAnchors();
  const bool chained = chainTrusted(anchors.get(), ssl, host);
  const std::string presented = fingerprint(SSL_get0_peer_certificate(ssl));
  std::lock_guard<std::mutex> lock(mutex_);
  if (chained) {
    pending_.clear();
    return Verdict::Trusted;
  }
  if (!pinning_ || presented.empty()) return Verdict::Refused;
  if (pin_.size() == presented.size() && CRYPTO_memcmp(pin_.data(), presented.data(), pin_.size()) == 0) {
    pending_.clear();
    return Verdict::Trusted;
  }
  if (pending_ != presented) {
    pending_ = presented;
    announced_ = false;
  }
  return Verdict::Pending;
}

bool PeerTrust::pinning() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pinning_;
}

std::string PeerTrust::pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_;
}

bool PeerTrust::takeNewPending(std::string& fingerprint) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (announced_ || pending_.empty()) return false;
  announced_ = true;
  fingerprint = pending_;
  return true;
}

}

namespace awtrix {

void useTlsTrust(httplib::Client& client) {
  if (!tls::publicAnchorsNamed()) return;
  client.set_server_certificate_verifier([](SSL* ssl) {
    const char* name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    const auto anchors = tls::publicAnchors();
    return name && tls::chainTrusted(anchors.get(), ssl, name) ? httplib::SSLVerifierResponse::CertificateAccepted
                                                               : httplib::SSLVerifierResponse::CertificateRejected;
  });
}

SSL_CTX* newTrustedTlsContext() {
  const auto anchors = tls::publicAnchors();
  if (!anchors) return nullptr;
  SSL_CTX* context = SSL_CTX_new(TLS_client_method());
  if (!context || !tls::applyProtocolPolicy(*context)) {
    SSL_CTX_free(context);
    return nullptr;
  }
  SSL_CTX_set1_cert_store(context, anchors->store());
  SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
  return context;
}

}
