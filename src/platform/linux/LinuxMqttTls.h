#pragma once

#include <memory>
#include <string>
#include "platform/linux/mqtt/IMqttTransport.h"

namespace awtrix {
namespace tls { class PeerTrust; }

// TLS byte transport for the shared MQTT stack. Trust is checked before any MQTT byte is sent.
class LinuxMqttTls final : public IMqttTransport, public Client {
 public:
  explicit LinuxMqttTls(std::shared_ptr<tls::PeerTrust> trust);
  ~LinuxMqttTls() override;
  bool valid() const;
  Client& socket() override { return *this; }
  void setPeerName(const std::string& host) override;
  net::ResolveState connectStep(std::function<bool()> handshake) override;
  void shutdown() override;

  int connect(IPAddress ip, uint16_t port) override;
  int connect(const char* address, uint16_t port) override;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  int available() override;
  int read() override;
  int read(uint8_t* buffer, size_t size) override;
  int peek() override;
  void flush() override;
  void stop() override;
  uint8_t connected() override;
  operator bool() override { return connected() != 0; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
