#include "transport/net/HostResolver.h"

#include <WiFi.h>
#include "platform/rp2040/BoundedHostLookup.h"
#include "system/Watchdog.h"

namespace awtrix::net {
namespace {
class HostResolverPico final : public IHostResolver {
 public:
  ResolveState resolve(const std::string& host) override {
    watchdog::feed();
    const bool ready = lookup_.resolve(WiFi, host);
    watchdog::feed();
    return ready ? ResolveState::Ready : ResolveState::Failed;
  }
  IPAddress address() const override { return lookup_.address(); }
  LinkError error() const override { return lookup_.error(); }
  void forget() override { lookup_.forget(); }
 private:
  platform::pico::BoundedHostLookup<IPAddress> lookup_;
};
}

std::unique_ptr<IHostResolver> makeHostResolver() {
  return std::unique_ptr<IHostResolver>(new HostResolverPico());
}
}
