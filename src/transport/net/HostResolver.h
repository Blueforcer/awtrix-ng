#pragma once

#include <IPAddress.h>

#include <cstdint>
#include <memory>
#include <string>

#include "core/net/HostName.h"
#include "core/net/LinkStatus.h"

namespace awtrix {
namespace net {

enum class ResolveState : uint8_t { Pending, Ready, Failed };

class IHostResolver {
 public:
  virtual ~IHostResolver() = default;

  // Poll until Ready or Failed. ESP32/simulator are asynchronous; Pico uses bounded calls
  // (1 s per lookup, at most 2 s with the .local bare-label fallback). IPv4 is immediate.
  virtual ResolveState resolve(const std::string& host) = 0;

  virtual IPAddress address() const = 0;

  virtual LinkError error() const = 0;

  // Drops the cached address and cancels anything in flight. Call it when the network changes or
  // the cached address stops working.
  virtual void forget() = 0;
};

std::unique_ptr<IHostResolver> makeHostResolver();

}
}
