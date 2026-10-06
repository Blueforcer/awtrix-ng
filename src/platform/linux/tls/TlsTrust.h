#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace httplib { class Client; }
struct ssl_ctx_st;
struct ssl_st;
struct x509_store_st;

namespace awtrix::tls {

// An immutable set of certificate authorities a peer's chain may end in.
class TrustAnchors {
 public:
  static std::shared_ptr<const TrustAnchors> fromPem(const std::string& pem);
  static std::shared_ptr<const TrustAnchors> fromFile(const std::string& path);
  ~TrustAnchors();
  x509_store_st* store() const { return store_; }

  TrustAnchors(const TrustAnchors&) = delete;
  TrustAnchors& operator=(const TrustAnchors&) = delete;

 private:
  explicit TrustAnchors(x509_store_st* store) : store_(store) {}
  // Takes the store; nullptr when it holds no certificate.
  static std::shared_ptr<const TrustAnchors> adopt(x509_store_st* store);
  x509_store_st* store_;
};

// The public roots (--ca-file) that every outgoing connection trusts unless told otherwise.
void setPublicAnchors(std::shared_ptr<const TrustAnchors> anchors);
std::shared_ptr<const TrustAnchors> publicAnchors();

// Server name indication and the name a verified chain must carry: a DNS name or an IP literal.
bool expectPeer(ssl_st* ssl, const std::string& host);
// Whether the completed handshake's chain ends in anchors and names host.
bool chainTrusted(const TrustAnchors* anchors, ssl_st* ssl, const std::string& host);

// Who a TLS client accepts after the handshake and before it sends anything: a chain that ends in
// its anchors and names the peer, or, where pinning is allowed, a certificate whose SHA-256 the
// user trusted. With pinning, an unknown certificate is kept as pending for that decision. The
// loop thread configures it; connection threads check against it.
class PeerTrust {
 public:
  enum class Verdict { Trusted, Pending, Refused };
  PeerTrust(std::shared_ptr<const TrustAnchors> anchors, bool pinning);
  void setAnchors(std::shared_ptr<const TrustAnchors> anchors, bool pinning);
  void setPin(const std::string& fingerprint);
  Verdict check(ssl_st* ssl, const std::string& host);
  bool pinning() const;
  std::string pending() const;
  // A fingerprint that became pending since the last call, once.
  bool takeNewPending(std::string& fingerprint);

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<const TrustAnchors> anchors_;
  bool pinning_;
  std::string pin_, pending_;
  bool announced_ = true;
};

}

namespace awtrix {
// Outgoing HTTPS of scripts, covers, radio and voice verifies against the public anchors; with
// none loaded every connection fails.
bool loadTlsTrust(const std::string& caFile, std::string& error);
void useTlsTrust(httplib::Client& client);
// A TLS 1.2+ client context that verifies peers against the public anchors during the handshake;
// nullptr while none are loaded.
ssl_ctx_st* newTrustedTlsContext();
inline bool expectTlsPeer(ssl_st* ssl, const std::string& host) { return tls::expectPeer(ssl, host); }
}
