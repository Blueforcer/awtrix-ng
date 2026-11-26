#include "transport/net/HostLookup.h"

#include <utility>

namespace awtrix {
namespace net {

void HostLookup::setHost(std::string host) {
  host_ = std::move(host);
  forget();
  retryAtMs_ = 0;
}

void HostLookup::forget() {
  if (resolver_) resolver_->forget();
  busy_ = false;
}

HostLookup::Result HostLookup::poll(int64_t nowMs, bool known, bool heard) {
  if (host_.empty() || nowMs < retryAtMs_) return Result::Idle;
  if (known && !busy_) {
    if (heard || nowMs - resolvedAtMs_ < kRelookMs) return Result::Idle;
    if (resolver_) resolver_->forget();
  }
  if (!resolver_) resolver_ = makeHostResolver();
  if (!resolver_) return Result::Idle;
  busy_ = true;
  switch (resolver_->resolve(host_)) {
    case ResolveState::Pending:
      return Result::Pending;
    case ResolveState::Failed:
      resolver_->forget();
      busy_ = false;
      resolvedAtMs_ = nowMs;
      retryAtMs_ = nowMs + kRetryMs;
      return Result::Failed;
    case ResolveState::Ready:
      break;
  }
  busy_ = false;
  resolvedAtMs_ = nowMs;
  return Result::Found;
}

}
}
