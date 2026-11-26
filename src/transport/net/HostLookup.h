#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "transport/net/HostResolver.h"

namespace awtrix {
namespace net {

class HostLookup {
 public:
  static constexpr int64_t kRetryMs = 10000;
  static constexpr int64_t kRelookMs = 30000;

  enum class Result : uint8_t { Idle, Pending, Failed, Found };

  const std::string& host() const { return host_; }
  void setHost(std::string host);
  void forget();
  Result poll(int64_t nowMs, bool known, bool heard);
  uint32_t address() const { return resolver_ ? resolver_->address() : 0; }

 private:
  std::unique_ptr<IHostResolver> resolver_;
  std::string host_;
  bool busy_ = false;
  int64_t retryAtMs_ = 0;
  int64_t resolvedAtMs_ = 0;
};

}
}
