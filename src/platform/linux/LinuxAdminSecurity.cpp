#include "platform/linux/host/HostStore.h"
#include "platform/linux/Auth.h"
#include "core/payload/Base64.h"
#include "platform/linux/LinuxAdminSecurity.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <openssl/crypto.h>
#include <openssl/sha.h>

#include "core/api/ApiRouter.h"
#include "core/net/Url.h"
#include "platform/linux/host/HostHttpServer.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/tls/ServerIdentity.h"

namespace awtrix {
namespace {
bool readProvisioned(const std::string& name, const std::filesystem::path& data,
                     bool secret, std::string& contents) {
  if (name.empty()) return false;
  std::error_code error;
  const auto path = std::filesystem::weakly_canonical(name, error);
  if (error || host::contains(data, path)) return false;
  struct stat parent{};
  if (::stat(path.parent_path().c_str(), &parent) != 0 || (parent.st_mode & 0022) != 0 ||
      (parent.st_uid != 0 && parent.st_uid != ::geteuid())) return false;
  // Open the original name: canonicalizing must not make a final symlink acceptable.
  const int fd = ::open(name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) return false;
  struct stat status{};
  const bool valid = ::fstat(fd, &status) == 0 && S_ISREG(status.st_mode) &&
      (status.st_uid == 0 || status.st_uid == ::geteuid()) &&
      (status.st_mode & (secret ? 0077 : 0022)) == 0 && status.st_size > 0 && status.st_size <= 65536;
  contents.clear();
  bool ok = valid;
  if (valid) {
    std::array<char, 4096> buffer{};
    ssize_t count;
    while ((count = ::read(fd, buffer.data(), buffer.size())) > 0) {
      contents.append(buffer.data(), static_cast<size_t>(count));
      if (contents.size() > 65536) { ok = false; break; }
    }
    if (count < 0) ok = false;
    OPENSSL_cleanse(buffer.data(), buffer.size());
  }
  ::close(fd);
  if (!ok) auth::wipe(contents);
  return ok;
}

bool parseOrigin(const std::string& origin, uint16_t port, std::string& authority, std::string& host) {
  const auto parsed = net::parseUrl(origin);
  if (!parsed || origin.rfind("https://", 0) != 0 || parsed->userinfo || !parsed->originOnly())
    return false;
  host = std::string(parsed->host);
  if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.') return false;
  for (const unsigned char c : host)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
  if (parsed->effectivePort != port ||
      (!parsed->port.empty() && parsed->port != std::to_string(port))) return false;
  authority = host;
  if (!parsed->port.empty()) { authority += ':'; authority += parsed->port; }
  return true;
}

// The hardened listener: the shared TLS listener with the provisioned identity, and the browser
// policy of an administration origin.
std::unique_ptr<httplib::Server> hardenedListener(const tls::ServerIdentity& identity) {
  auto server = tls::newTlsServer(identity);
  if (!server) return nullptr;
  server->set_default_headers({{"X-Content-Type-Options", "nosniff"},
      {"X-Frame-Options", "DENY"}, {"Referrer-Policy", "no-referrer"},
      {"Strict-Transport-Security", "max-age=31536000"},
      {"Content-Security-Policy", "frame-ancestors 'none'; object-src 'none'; base-uri 'self'"}});
  server->set_post_routing_handler([](const httplib::Request&, httplib::Response& response) {
    response.headers.erase("Cache-Control");
    response.set_header("Cache-Control", "no-store");
  });
  return server;
}


}

bool configureLinuxAdminSecurity(const LinuxAdminConfig& config, const std::string& dataDirectory,
                                 uint16_t port, HostHttpOptions& options, std::string& error) {
  const bool configured = !config.credentialsFile.empty() || !config.certificateFile.empty() ||
      !config.privateKeyFile.empty() || !config.origin.empty() || !config.listenAddress.empty() || !config.mqttCaFile.empty();
  if (config.lan) {
    if (config.hardened) { error = "--lan and --hardened are mutually exclusive"; return false; }
    if (!config.credentialsFile.empty() || !config.certificateFile.empty() || !config.privateKeyFile.empty() ||
        !config.origin.empty() || !config.mqttCaFile.empty()) { error = "--lan accepts only --listen"; return false; }
    const std::string listen = config.listenAddress.empty() ? "0.0.0.0" : config.listenAddress;
    in_addr address{};
    if (::inet_pton(AF_INET, listen.c_str(), &address) != 1) {
      error = "--listen must be an IPv4 address"; return false;
    }
    options.listenAddress = listen;
    options.allowCrossOrigin = true;
    return true;
  }
  if (!config.hardened) {
    if (configured) { error = "security/listen options require --hardened"; return false; }
    return true;
  }
  if (::geteuid() == 0) { error = "hardened mode requires an unprivileged user"; return false; }
  std::string authority, host;
  if (!parseOrigin(config.origin, port, authority, host)) {
    error = "--origin must be an HTTPS DNS/IPv4 origin matching --port"; return false;
  }
  const std::string listen = config.listenAddress.empty() ? "127.0.0.1" : config.listenAddress;
  in_addr address{};
  if (::inet_pton(AF_INET, listen.c_str(), &address) != 1) {
    error = "--listen must be an IPv4 address"; return false;
  }
  std::error_code pathError;
  const auto data = std::filesystem::weakly_canonical(dataDirectory, pathError);
  if (pathError) { error = "invalid application data directory"; return false; }
  std::string token, certificate, key;
  if (!readProvisioned(config.credentialsFile, data, true, token)) {
    error = "credentials must be a protected regular file outside --data"; return false;
  }
  if (!token.empty() && token.back() == '\n') token.pop_back();
  if (!token.empty() && token.back() == '\r') token.pop_back();
  const bool validToken = token.size() == 64 && std::all_of(token.begin(), token.end(), [](unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  });
  if (!validToken) {
    auth::wipe(token); error = "credentials must contain exactly 64 hexadecimal characters"; return false;
  }
  std::string bearer = "Bearer " + token;
  std::string userPassword = "admin:" + token;
  std::string encoded = base64::encode(userPassword.data(), userPassword.size());
  std::string basic = "Basic " + encoded;
  const auth::Digest bearerHash = auth::digest(bearer), basicHash = auth::digest(basic);
  auth::wipe(token); auth::wipe(bearer); auth::wipe(userPassword); auth::wipe(encoded); auth::wipe(basic);
  if (!readProvisioned(config.certificateFile, data, false, certificate) ||
      !readProvisioned(config.privateKeyFile, data, true, key)) {
    auth::wipe(key); error = "TLS certificate/key must be protected regular files outside --data"; return false;
  }
  // Validate now, before application services start. The live listener presents this
  // identity, not files reopened after the permission checks.
  std::string invalid;
  auto identity = tls::ServerIdentity::fromPem(certificate, key, std::time(nullptr), invalid);
  auth::wipe(key);
  auto listener = identity && identity->names(host) ? hardenedListener(*identity) : nullptr;
  if (!listener) { error = "TLS certificate/key invalid, expired, mismatched, or missing origin SAN"; return false; }
  auto pending = std::make_shared<std::unique_ptr<httplib::Server>>(std::move(listener));
  options.listenAddress = listen;
  options.listenerFactory = [pending]() { return std::move(*pending); };
  options.requestGuard = [authority, origin = config.origin, bearerHash, basicHash]
      (const httplib::Request& request, httplib::Response& response) {
    if (request.get_header_value_count("Host") != 1 || request.get_header_value("Host") != authority ||
        request.get_header_value_count("Origin") > 1 ||
        (request.has_header("Origin") && request.get_header_value("Origin") != origin) ||
        request.get_header_value_count("Sec-Fetch-Site") > 1 ||
        request.get_header_value("Sec-Fetch-Site") == "cross-site")
      return auth::deny(response, 403, "forbiddenOrigin", "request origin is not allowed");
    const auto authorization = request.get_header_value("Authorization");
    const auth::Digest candidate = auth::digest(authorization);
    const bool matches = auth::equal(candidate, bearerHash) |
                         auth::equal(candidate, basicHash);
    if (request.get_header_value_count("Authorization") != 1 || !matches) {
      return auth::unauthorized(response, "Basic realm=\"AWTRIX administration\", charset=\"UTF-8\"",
                                "administrator credentials required");
    }
    return true;
  };
  return true;
}

bool readLinuxProvisionedFile(const std::string& file, const std::string& dataDirectory,
                              bool secret, std::string& contents) {
  std::error_code error;
  const auto data = std::filesystem::weakly_canonical(dataDirectory, error);
  return !error && readProvisioned(file, data, secret, contents);
}
}
