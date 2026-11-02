#include "platform/linux/tls/TrustStore.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/Sha256Hex.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/posix/Files.h"
#include "system/Log.h"
#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::tls {
namespace {
constexpr std::size_t kMaxStoreBytes = TrustStore::kMaxHosts * (2 * TrustStore::kMaxCaBytes + 1024);
}

TrustStore::TrustStore(std::string directory) : directory_(std::move(directory)) {
  usable_ = load();
  if (!usable_) logf("tls: trusted certificate store unusable; connections refused");
}

std::string TrustStore::normalizeHost(const std::string& host) {
  if (host.empty() || host.size() > 253 || host.find('\0') != host.npos) return {};
  unsigned char address[16];
  char text[INET6_ADDRSTRLEN];
  for (const int family : {AF_INET, AF_INET6}) {
    if (::inet_pton(family, host.c_str(), address) == 1) {
      if (::inet_ntop(family, address, text, sizeof(text))) return text;
      return {};
    }
  }
  if (host.find('.') != host.npos && host.find_first_not_of("0123456789.") == host.npos) return {};
  std::string name = host;
  if (name.back() == '.') name.pop_back();
  std::size_t label = 0;
  for (char& c : name) {
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c == '.') {
      if (!label || label > 63) return {};
      label = 0;
    } else {
      if ((c < 'a' || c > 'z') && (c < '0' || c > '9') && c != '-') return {};
      ++label;
    }
  }
  if (!label || label > 63) return {};
  std::size_t begin = 0;
  while (begin < name.size()) {
    const auto end = name.find('.', begin);
    const auto last = end == name.npos ? name.size() : end;
    if (name[begin] == '-' || name[last - 1] == '-') return {};
    begin = last + 1;
  }
  return name;
}

bool TrustStore::load() {
  auto directory = posix::openPrivateDirectory(directory_, true);
  if (!directory.valid()) return false;
  struct stat st {};
  if (::fstatat(directory.get(), "trust.json", &st, AT_SYMLINK_NOFOLLOW) != 0) return errno == ENOENT;
  if (!S_ISREG(st.st_mode) || st.st_uid != ::geteuid() || (st.st_mode & 0077)) return false;
  std::string json;
  if (!posix::readRegularFile(directory.get(), "trust.json", kMaxStoreBytes, json) ||
      !api::isWellFormed(json)) return false;
  long long version = 0;
  if (!api::memberValue(json, "version").asLong(version) || version != 1) return false;
  auto rows = api::memberValue(json, "hosts");
  if (!rows.enterArray()) return false;
  Entries next;
  while (rows.nextElement()) {
    std::string host, pin, ca;
    if (!rows.isObject() || !api::memberValue(rows, "host").appendString(host) ||
        !api::memberValue(rows, "pin").appendString(pin) || !api::memberValue(rows, "ca").appendString(ca)) return false;
    if (host.empty() || host != normalizeHost(host) || next.count(host) || next.size() >= kMaxHosts ||
        (!pin.empty() && !isSha256Hex(pin)) || ca.size() > kMaxCaBytes || (pin.empty() && ca.empty())) return false;
    auto anchors = ca.empty() ? nullptr : TrustAnchors::fromCaPem(ca);
    if (!ca.empty() && !anchors) return false;
    next.emplace(host, Entry{pin, ca, std::move(anchors)});
    if (!rows.skipValue()) return false;
  }
  if (!rows.ok()) return false;
  entries_ = std::move(next);
  return true;
}

bool TrustStore::save(const Entries& entries) {
  std::string json;
  api::JsonWriter w(json);
  w.beginObject().member("version", 1).key("hosts").beginArray();
  for (const auto& [host, entry] : entries)
    w.beginObject().member("host", host).member("pin", entry.pin).member("ca", entry.ca).endObject();
  w.endArray().endObject();
  return json.size() <= kMaxStoreBytes && posix::ensurePrivateDirectory(directory_) &&
      posix::replaceText(directory_ + "/trust.json", json);
}

PeerTrust::Verdict TrustStore::check(ssl_st* ssl, const std::string& name, bool* customTrust) {
  const auto host = normalizeHost(name);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!usable_ || host.empty()) return PeerTrust::Verdict::Refused;
  const auto found = entries_.find(host);
  if (customTrust) *customTrust = found != entries_.end();
  PeerTrust verifier(found != entries_.end() && found->second.anchors ? found->second.anchors : publicAnchors(), true);
  if (found != entries_.end()) verifier.setPin(found->second.pin);
  const auto result = verifier.check(ssl, host);
  if (result == PeerTrust::Verdict::Trusted) pending_.erase(host);
  else if (result == PeerTrust::Verdict::Pending && (pending_.count(host) || pending_.size() < kMaxPending)) {
    const auto fingerprint = verifier.pending();
    if (pending_[host] != fingerprint) {
      pending_[host] = fingerprint;
      logf("tls: %.20s untrusted SHA-256 %s", host.c_str(), fingerprint.c_str());
    }
  }
  return result;
}

bool TrustStore::custom(const std::string& host) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !usable_ || entries_.count(normalizeHost(host));
}

std::string TrustStore::pending(const std::string& host) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = pending_.find(normalizeHost(host));
  return found == pending_.end() ? std::string{} : found->second;
}

std::string TrustStore::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string json;
  api::JsonWriter w(json);
  w.beginObject().member("usable", usable_).key("hosts").beginArray();
  auto hosts = pending_;
  for (const auto& [host, entry] : entries_) hosts.emplace(host, "");
  for (const auto& [host, unused] : hosts) {
    const auto saved = entries_.find(host);
    const auto pending = pending_.find(host);
    w.beginObject().member("host", host).member("ca", saved != entries_.end() && !saved->second.ca.empty());
    if (saved == entries_.end() || saved->second.pin.empty()) w.memberNull("pin");
    else w.member("pin", saved->second.pin);
    if (pending == pending_.end()) w.memberNull("pending");
    else w.member("pending", pending->second);
    w.endObject();
  }
  w.endArray().endObject();
  return json;
}

int TrustStore::uploadCa(const std::string& name, const std::string& pem) {
  const auto host = normalizeHost(name);
  if (host.empty() || pem.size() > kMaxCaBytes) return 422;
  auto anchors = TrustAnchors::fromCaPem(pem);
  if (!anchors) return 422;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!usable_) return 503;
  if (!entries_.count(host) && entries_.size() >= kMaxHosts) return 409;
  auto next = entries_;
  next[host].ca = pem;
  next[host].anchors = std::move(anchors);
  if (!save(next)) return 507;
  entries_ = std::move(next);
  pending_.erase(host);
  return 200;
}

int TrustStore::approve(const std::string& name, const std::string& fingerprint) {
  const auto host = normalizeHost(name);
  if (host.empty() || !isSha256Hex(fingerprint)) return 422;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!usable_) return 503;
  const auto pending = pending_.find(host);
  if (pending == pending_.end() || pending->second != fingerprint) return 409;
  if (!entries_.count(host) && entries_.size() >= kMaxHosts) return 409;
  auto next = entries_;
  next[host].pin = fingerprint;
  if (!save(next)) return 507;
  entries_ = std::move(next);
  pending_.erase(pending);
  return 200;
}

int TrustStore::remove(const std::string& name, const std::string& part) {
  const auto host = normalizeHost(name);
  if (host.empty() || (!part.empty() && part != "pin" && part != "ca")) return 422;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!usable_) return 503;
  auto next = entries_;
  const auto found = next.find(host);
  if (found != next.end()) {
    if (part == "pin") found->second.pin.clear();
    else if (part == "ca") { found->second.ca.clear(); found->second.anchors.reset(); }
    if (part.empty() || (found->second.pin.empty() && found->second.ca.empty())) next.erase(found);
  }
  if (!save(next)) return 507;
  entries_ = std::move(next);
  pending_.erase(host);
  return 200;
}

bool TrustStore::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!save({})) return false;
  entries_.clear();
  pending_.clear();
  usable_ = true;
  return true;
}

void useHostTlsTrust(httplib::Client& client, std::shared_ptr<TrustStore> store, const std::string& host, bool* customTrust) {
  client.set_follow_location(false);
  client.set_server_certificate_verifier([store = std::move(store), host, customTrust](ssl_st* ssl) {
    return store && store->check(ssl, host, customTrust) == PeerTrust::Verdict::Trusted
        ? httplib::SSLVerifierResponse::CertificateAccepted : httplib::SSLVerifierResponse::CertificateRejected;
  });
}

}
