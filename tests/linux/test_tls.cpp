#include "../support.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include "core/api/JsonStream.h"
#include "persistence/DeviceConfig.h"
#include "persistence/SystemConfigApply.h"
#include "platform/linux/host/HostStore.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/LinuxMqttTls.h"
#include "platform/linux/tls/BrokerTrust.h"
#include "platform/linux/tls/BrokerTrustApi.h"
#include "platform/linux/tls/ServerIdentity.h"
#include "platform/linux/tls/TlsPolicy.h"
#include "platform/linux/tls/TlsTrust.h"
#include "system/Log.h"

using namespace awtrix;
using namespace awtrix::tls;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

int availablePort() {
  const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t size = sizeof(address);
  const bool okay = fd >= 0 && ::bind(fd, reinterpret_cast<sockaddr*>(&address), size) == 0 &&
      ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0;
  if (fd >= 0) ::close(fd);
  return okay ? ntohs(address.sin_port) : 0;
}

std::string pem(BIO* bio) {
  char* data = nullptr;
  const long size = BIO_get_mem_data(bio, &data);
  std::string text(data, static_cast<size_t>(size));
  BIO_free(bio);
  return text;
}

struct Pair {
  std::string certificate, key;
};

// A CA (san empty, issuer null), a self-signed server certificate (issuer null) or one the CA in
// issuer signed; valid from an hour ago for days.
Pair issued(const std::string& san, const Pair* issuer, long days = 1) {
  KeyPtr key(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256"), EVP_PKEY_free);
  X509Ptr certificate(X509_new(), X509_free);
  X509_set_version(certificate.get(), X509_VERSION_3);
  static long serial = 1;
  ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), ++serial);
  X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -3600);
  X509_gmtime_adj(X509_getm_notAfter(certificate.get()), days * 86400);
  X509_NAME_add_entry_by_txt(X509_get_subject_name(certificate.get()), "CN", MBSTRING_UTF8,
                             reinterpret_cast<const unsigned char*>(san.empty() ? "Test CA" : "server"), -1, -1, 0);
  X509_set_pubkey(certificate.get(), key.get());
  X509Ptr issuerCertificate(nullptr, X509_free);
  KeyPtr issuerKey(nullptr, EVP_PKEY_free);
  if (issuer) {
    issuerCertificate = std::move(readCertificates(issuer->certificate).front());
    issuerKey = readPrivateKey(issuer->key);
  }
  X509_set_issuer_name(certificate.get(), X509_get_subject_name(issuer ? issuerCertificate.get() : certificate.get()));
  X509V3_CTX context;
  X509V3_set_ctx(&context, issuer ? issuerCertificate.get() : certificate.get(), certificate.get(), nullptr, nullptr, 0);
  const auto add = [&](int nid, const std::string& value) {
    X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &context, nid, value.c_str());
    X509_add_ext(certificate.get(), extension, -1);
    X509_EXTENSION_free(extension);
  };
  if (san.empty()) {
    add(NID_basic_constraints, "critical,CA:TRUE");
    add(NID_key_usage, "critical,keyCertSign,cRLSign");
  } else {
    add(NID_basic_constraints, "critical,CA:FALSE");
    add(NID_subject_alt_name, san);
  }
  X509_sign(certificate.get(), issuer ? issuerKey.get() : key.get(), EVP_sha256());
  BIO* certificateOut = BIO_new(BIO_s_mem());
  PEM_write_bio_X509(certificateOut, certificate.get());
  BIO* keyOut = BIO_new(BIO_s_mem());
  PEM_write_bio_PrivateKey(keyOut, key.get(), nullptr, nullptr, 0, nullptr, nullptr);
  return {pem(certificateOut), pem(keyOut)};
}

std::string fingerprintOf(const Pair& pair) { return fingerprint(readCertificates(pair.certificate).front().get()); }

// A TLS listener with one identity; an MQTT connect only needs its handshake.
struct Broker {
  std::shared_ptr<const ServerIdentity> identity;
  std::unique_ptr<httplib::Server> server;
  std::thread thread;
  int port = availablePort();
  explicit Broker(const Pair& pair) {
    std::string error;
    identity = ServerIdentity::fromPem(pair.certificate, pair.key, std::time(nullptr), error);
    check(identity != nullptr, "broker identity: " + error);
    if (identity) server = newTlsServer(*identity);
    check(server && server->bind_to_port("127.0.0.1", port), "broker listens");
    if (!server) return;
    thread = std::thread([this] { server->listen_after_bind(); });
    server->wait_until_ready();
  }
  ~Broker() {
    if (server) server->stop();
    if (thread.joinable()) thread.join();
  }
};

// A listener that hands each of its first connections to serve on its own thread, then closes it.
struct RawBroker {
  int listener = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  int port = 0;
  std::thread thread;
  RawBroker(int connections, std::function<void(int)> serve) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    const bool listening = listener >= 0 && ::bind(listener, reinterpret_cast<sockaddr*>(&address), size) == 0 &&
        ::listen(listener, 4) == 0 && ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0;
    check(listening, "raw broker listens");
    if (!listening) return;
    port = ntohs(address.sin_port);
    thread = std::thread([this, connections, serve = std::move(serve)] {
      for (int i = 0; i < connections; ++i) {
        const int fd = ::accept(listener, nullptr, nullptr);
        if (fd < 0) return;
        serve(fd);
        ::close(fd);
      }
    });
  }
  ~RawBroker() {
    if (thread.joinable()) thread.join();
    if (listener >= 0) ::close(listener);
  }
};

// The log as GET /api/v1/logs?after= answers: {"next":N,"lines":[...]}.
std::string logAfter(uint32_t after) {
  std::string text;
  api::JsonStream out([](void* context, const char* data, size_t size) {
    static_cast<std::string*>(context)->append(data, size);
  }, &text);
  logbuf::streamJsonAfter(after, out);
  return text;
}

uint32_t logMark() {
  return static_cast<uint32_t>(std::strtoul(logAfter(~0u).c_str() + std::strlen("{\"next\":"), nullptr, 10));
}

// The log lines written since mark, which moves on past them.
std::string logSince(uint32_t& mark) {
  const std::string text = logAfter(mark);
  mark = logMark();
  return text;
}

bool connects(const std::shared_ptr<PeerTrust>& trust, const std::string& peer, int port) {
  LinuxMqttTls transport(trust);
  transport.setPeerName(peer);
  const bool connected = transport.connect("127.0.0.1", static_cast<uint16_t>(port)) == 1;
  transport.shutdown();
  return connected;
}

void identityChecks() {
  std::string error;
  const std::time_t now = std::time(nullptr);
  const Pair a = issued("DNS:a", nullptr), b = issued("DNS:b", nullptr);
  check(!ServerIdentity::fromPem(a.certificate, b.key, now, error) && error == "key does not match", "mismatch: " + error);
  check(!ServerIdentity::fromPem("garbage", a.key, now, error) && error == "invalid certificate", "garbage: " + error);
  check(!ServerIdentity::fromPem(a.certificate, "garbage", now, error) && error == "invalid key", "bad key: " + error);
  check(!ServerIdentity::fromPem(a.certificate, a.key, now + 2 * 86400, error) &&
            error == "certificate not valid now", "expired: " + error);
  check(!ServerIdentity::fromPem(a.certificate, a.key, now - 2 * 86400, error) &&
            error == "certificate not valid now", "not yet valid: " + error);
  const Pair ca = issued("", nullptr), leaf = issued("DNS:admin.test", &ca);
  const auto chained = ServerIdentity::fromPem(leaf.certificate + ca.certificate, leaf.key, now, error);
  check(chained && chained->names("admin.test") && !chained->names("other.test"), "a certificate with its chain: " + error);
}

void serverPresentsItsChain() {
  const Pair ca = issued("", nullptr), leaf = issued("DNS:localhost", &ca);
  Broker server(Pair{leaf.certificate + ca.certificate, leaf.key});
  auto trust = std::make_shared<PeerTrust>(TrustAnchors::fromPem(ca.certificate), false);
  check(connects(trust, "localhost", server.port), "the listener presents certificate and chain");
}

void brokerTrust() {
  const Pair own = issued("DNS:localhost,IP:127.0.0.1", nullptr);
  const std::string print = fingerprintOf(own);
  auto pinned = std::make_shared<PeerTrust>(nullptr, true);
  {
    Broker broker(own);
    check(!connects(pinned, "127.0.0.1", broker.port), "an unknown broker is refused");
    check(pinned->pending() == print, "its fingerprint waits for a decision");
    std::string announced;
    check(pinned->takeNewPending(announced) && announced == print, "a new pending fingerprint is announced");
    check(!pinned->takeNewPending(announced), "and announced once");
    check(!connects(pinned, "127.0.0.1", broker.port), "still refused before the decision");
    check(!pinned->takeNewPending(announced), "the same fingerprint is not announced again");
    pinned->setPin(print);
    check(pinned->pending().empty(), "trusting the pending fingerprint clears it");
    check(connects(pinned, "127.0.0.1", broker.port), "the pinned broker is used");
    check(connects(pinned, "any-name.invalid", broker.port), "the pin is the identity, whatever the host name");
  }
  const Pair replaced = issued("DNS:localhost,IP:127.0.0.1", nullptr);
  {
    Broker broker(replaced);
    check(!connects(pinned, "127.0.0.1", broker.port), "a changed certificate is refused");
    check(pinned->pending() == fingerprintOf(replaced), "and waits for a new decision");
  }
  const Pair ca = issued("", nullptr), leaf = issued("DNS:localhost", &ca);
  Broker broker(Pair{leaf.certificate + ca.certificate, leaf.key});
  auto publicRoots = std::make_shared<PeerTrust>(TrustAnchors::fromPem(ca.certificate), true);
  publicRoots->setPin(print);
  check(connects(publicRoots, "localhost", broker.port), "a broker that chains to the roots and names the host");
  check(publicRoots->pending().empty(), "needs no decision");
  check(!connects(publicRoots, "127.0.0.1", broker.port), "a chained certificate for another name is refused");
  check(publicRoots->pending() == fingerprintOf(leaf), "and can then be trusted by fingerprint");

  auto uploaded = std::make_shared<PeerTrust>(TrustAnchors::fromPem(ca.certificate), false);
  check(connects(uploaded, "localhost", broker.port), "an uploaded CA verifies its broker");
  check(!connects(uploaded, "127.0.0.1", broker.port) && uploaded->pending().empty(),
        "with an uploaded CA nothing else is accepted or offered");
  auto otherCa = std::make_shared<PeerTrust>(TrustAnchors::fromPem(issued("", nullptr).certificate), false);
  check(!connects(otherCa, "localhost", broker.port), "another CA's broker is refused");

  check(!TrustAnchors::fromPem("no certificate"), "a CA upload without a certificate is refused");
  check(!LinuxMqttTls(nullptr).valid(), "a transport without trust is not usable");
}

void brokerTrustApi(const std::filesystem::path& data) {
  BrokerTrust broker("");
  BrokerTrustApi api(broker);
  std::string body;
  check(api.handle("GET", "/api/v1/mqtt/tls", "", body) == 200 && has(body, "\"ca\":\"public\"") &&
            has(body, "\"pending\":null"), "the status: " + body);
  check(api.handle("POST", "/api/v1/mqtt/tls", "{}", body) == 405, "the status is read-only");
  check(api.handle("PUT", "/api/v1/mqtt/tls/ca", "{\"certificate\":\"x\"}", body) == 422 &&
            has(body, "invalid certificate") && has(body, "\"field\":\"certificate\""), "a broken CA is refused: " + body);
  check(api.handle("PUT", "/api/v1/mqtt/tls/ca", "{}", body) == 422 && has(body, "expected a PEM string"),
        "a missing CA is refused: " + body);
  std::string request;
  api::JsonWriter w(request);
  w.beginObject().member("certificate", issued("", nullptr).certificate).endObject();
  check(api.handle("PUT", "/api/v1/mqtt/tls/ca", request, body) == 200 && has(body, "\"ca\":\"uploaded\""),
        "a CA is uploaded: " + body);
  check(std::filesystem::exists(data / "tls" / "mqtt-ca.pem"), "and stored");
  BrokerTrust restarted("");
  check(restarted.ca() == BrokerTrust::Ca::Uploaded && !restarted.peer()->pinning(), "a restart keeps it, without pinning");

  const std::string stored = issued("", nullptr).certificate;
  std::string large = stored;
  while (large.size() <= BrokerTrust::kMaxCaBytes) large += stored;
  request.clear();
  api::JsonWriter big(request);
  big.beginObject().member("certificate", large).endObject();
  check(api.handle("PUT", "/api/v1/mqtt/tls/ca", request, body) == 422 && has(body, "at most 65536 bytes") &&
            has(body, "\"field\":\"certificate\""), "a CA larger than a restart reads is refused: " + body);
  check(host::writeFile((data / "tls" / "mqtt-ca.pem").string(), large), "an oversized CA file");
  check(BrokerTrust("").ca() == BrokerTrust::Ca::Unusable, "is not read back as public trust");
  check(api.handle("DELETE", "/api/v1/mqtt/tls/ca", "", body) == 200 && has(body, "\"ca\":\"public\""), "and removed");
  check(broker.peer()->pinning() && !std::filesystem::exists(data / "tls" / "mqtt-ca.pem"),
        "then public CAs and pins count again");
  check(api.handle("GET", "/api/v1/other", "", body) == 0, "other paths are not its own");
}

// A stored CA that cannot be used refuses every broker, even one the public roots or the pin would
// accept, and says so.
void unusableCa(const std::filesystem::path& data) {
  const Pair ca = issued("", nullptr), leaf = issued("DNS:localhost", &ca);
  setPublicAnchors(TrustAnchors::fromPem(ca.certificate));
  Broker server(Pair{leaf.certificate + ca.certificate, leaf.key});
  check(host::writeFile((data / "tls" / "mqtt-ca.pem").string(), "not a certificate"), "a broken CA file");
  uint32_t mark = logMark();
  BrokerTrust broker(fingerprintOf(leaf));
  BrokerTrustApi api(broker);
  check(broker.ca() == BrokerTrust::Ca::Unusable && !broker.peer()->pinning(), "is unusable, without pinning");
  check(has(logSince(mark), "mqtt: uploaded CA unusable, broker refused"), "and logged");
  std::string body;
  check(api.handle("GET", "/api/v1/mqtt/tls", "", body) == 200 && has(body, "\"ca\":\"unusable\""), "reported: " + body);
  check(!connects(broker.peer(), "localhost", server.port) && broker.peer()->pending().empty(),
        "no broker is accepted or offered");
  std::string request;
  api::JsonWriter w(request);
  w.beginObject().member("certificate", ca.certificate).endObject();
  check(api.handle("PUT", "/api/v1/mqtt/tls/ca", request, body) == 200 && has(body, "\"ca\":\"uploaded\""),
        "a new upload replaces it: " + body);
  check(connects(broker.peer(), "localhost", server.port), "and its broker is used");
  check(host::writeFile((data / "tls" / "mqtt-ca.pem").string(), "not a certificate"), "broken again");
  BrokerTrust again("");
  BrokerTrustApi againApi(again);
  check(againApi.handle("DELETE", "/api/v1/mqtt/tls/ca", "", body) == 200 && has(body, "\"ca\":\"public\"") &&
            connects(again.peer(), "localhost", server.port), "deleting it goes back to the public roots: " + body);
  setPublicAnchors(nullptr);
}

// One connection attempt as MqttLink makes it: handshake, the CONNECT, then waiting for an answer.
bool attempt(LinuxMqttTls& transport, int port) {
  const auto handshake = [&transport, port] {
    if (transport.connect("127.0.0.1", static_cast<uint16_t>(port)) != 1) return false;
    const uint8_t connect[] = {0x10, 0x00};
    transport.write(connect, sizeof(connect));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline && transport.connected() && !transport.available())
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return transport.available() > 0;
  };
  net::ResolveState state;
  while ((state = transport.connectStep(handshake)) == net::ResolveState::Pending)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  return state == net::ResolveState::Ready;
}

size_t count(const std::string& text, const std::string& part) {
  size_t found = 0;
  for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) ++found;
  return found;
}

// Why a broker ended the connection reaches the log, once per reason and not on every retry.
void brokerAlerts() {
  const Pair ca = issued("", nullptr), leaf = issued("DNS:localhost", &ca);
  auto trust = std::make_shared<PeerTrust>(TrustAnchors::fromPem(ca.certificate), false);
  // The broker fails the TLS 1.3 handshake after the client's Finished, when the client already
  // sends. It either resets the connection at once or reads everything first.
  for (const bool lingers : {false, true}) {
    const std::string how = lingers ? " (lingering broker)" : " (resetting broker)";
    LinuxMqttTls transport(trust);
    transport.setPeerName("localhost");
    RawBroker broker(2, [&leaf, lingers](int fd) {
      ContextPtr context(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
      SSL_CTX_set_min_proto_version(context.get(), TLS1_3_VERSION);
      SSL_CTX_use_certificate(context.get(), readCertificates(leaf.certificate).front().get());
      SSL_CTX_use_PrivateKey(context.get(), readPrivateKey(leaf.key).get());
      SSL_CTX_set_verify(context.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
      SSL* ssl = SSL_new(context.get());
      SSL_set_fd(ssl, fd);
      SSL_accept(ssl);
      SSL_free(ssl);
      if (!lingers) return;
      ::shutdown(fd, SHUT_WR);
      char rest[512];
      while (::recv(fd, rest, sizeof(rest), 0) > 0) {}
    });
    uint32_t mark = logMark();
    check(!attempt(transport, broker.port), "a broker that wants a client certificate refuses the client" + how);
    check(!attempt(transport, broker.port), "on every attempt" + how);
    const std::string log = logSince(mark);
    check(count(log, "mqtt: broker requires a client certificate") == 1, "which is logged once" + how + ": " + log);
  }
  {
    LinuxMqttTls transport(trust);
    transport.setPeerName("localhost");
    RawBroker broker(1, [](int fd) {
      char hello[512];
      if (::recv(fd, hello, sizeof(hello), 0) <= 0) return;
      const unsigned char handshakeFailure[] = {0x15, 0x03, 0x03, 0x00, 0x02, 0x02, 0x28};
      ::send(fd, handshakeFailure, sizeof(handshakeFailure), MSG_NOSIGNAL);
    });
    uint32_t mark = logMark();
    check(!attempt(transport, broker.port), "a broker that answers with an alert is not used");
    const std::string log = logSince(mark);
    check(has(log, "mqtt: broker closed TLS: handshake failure"), "and its alert is logged: " + log);
  }
}

void settings() {
  DeviceConfig config;
  check(!config.mqttTls && config.mqttTlsPin.empty(), "defaults");
  const std::string pin(64, 'a');
  int applied = 0;
  sysconfig::ApplyError failure;
  const std::string body = "{\"mqttTls\":true,\"mqttTlsPin\":\"" + pin + "\"}";
  check(sysconfig::apply(config, api::JsonReader(body), applied, failure) && applied == 2, "TLS settings apply");
  std::string written;
  api::JsonWriter writer(written);
  writer.beginObject();
  config.write(writer, false);
  writer.endObject();
  check(has(written, "\"mqttTls\":true") && has(written, "\"mqttTlsPin\":\"" + pin + "\""), "and are reported");
  DeviceConfig restored;
  restored.applyRead(api::JsonReader(written));
  check(restored.mqttTls && restored.mqttTlsPin == pin, "a written configuration reads back");
  for (const std::string& bad : {std::string("ABCD"), std::string(64, 'G'), std::string(64, 'A')}) {
    DeviceConfig target;
    sysconfig::ApplyError error;
    int count = 0;
    check(!sysconfig::apply(target, api::JsonReader("{\"mqttTlsPin\":\"" + bad + "\"}"), count, error) &&
              error.status == 422 && error.field == "mqttTlsPin", bad + " is refused: " + error.message);
  }
  check(sysconfig::apply(config, api::JsonReader("{\"mqttTlsPin\":\"\"}"), applied, failure) && config.mqttTlsPin.empty(),
        "an empty pin forgets the broker");
}

}

int main() {
  char directory[] = "/tmp/awtrix-tls-XXXXXX";
  if (!::mkdtemp(directory)) return 1;
  const std::filesystem::path data(directory);
  host::setDataDir(data.string());
  identityChecks();
  serverPresentsItsChain();
  brokerTrust();
  brokerTrustApi(data);
  unusableCa(data);
  brokerAlerts();
  settings();
  std::error_code ignored;
  std::filesystem::remove_all(data, ignored);
  std::printf("tls: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}
