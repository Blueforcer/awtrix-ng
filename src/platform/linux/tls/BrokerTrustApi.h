#pragma once

#include <string>

namespace awtrix::tls {
class BrokerTrust;

// /api/v1/mqtt/tls: how the MQTT client trusts its broker, and the uploaded broker CA. Runs on
// the main loop.
class BrokerTrustApi {
 public:
  explicit BrokerTrustApi(BrokerTrust& broker) : broker_(broker) {}
  // The HTTP status and JSON body, or 0 for a path that is not its own.
  int handle(const std::string& method, const std::string& path, const std::string& request, std::string& body);

 private:
  std::string status() const;
  BrokerTrust& broker_;
};

}
