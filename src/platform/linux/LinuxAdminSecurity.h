#pragma once

#include <cstdint>
#include <string>

namespace awtrix {
struct HostHttpOptions;

struct LinuxAdminConfig {
  bool hardened = false;
  bool lan = false;
  std::string credentialsFile, certificateFile, privateKeyFile, origin;
  std::string mqttCaFile;
  std::string listenAddress;
};

// Validate local provisioning before starting any application service. Credentials
// are never part of DeviceConfig, its API, backups, or application factory reset.
// The LAN service instead uses the device login from DeviceConfig (LinuxLanLogin).
bool configureLinuxAdminSecurity(const LinuxAdminConfig& config, const std::string& dataDirectory,
                                 uint16_t port, HostHttpOptions& options, std::string& error);
bool readLinuxProvisionedFile(const std::string& file, const std::string& dataDirectory,
                              bool secret, std::string& contents);
}
