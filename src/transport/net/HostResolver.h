#pragma once

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

  // Never blocks. Call it repeatedly with the same host until it returns Ready or Failed; a
  // literal IPv4 address is answered on the spot.
  virtual ResolveState resolve(const std::string& host) = 0;

  // IPv4 in host byte order, matching net::Endpoint (127.0.0.1 is 0x7f000001).
  virtual uint32_t address() const = 0;

  virtual LinkError error() const = 0;

  // Drops the cached address and cancels anything in flight. Call it when the network changes or
  // the cached address stops working.
  virtual void forget() = 0;
};

std::unique_ptr<IHostResolver> makeHostResolver();

}
}
