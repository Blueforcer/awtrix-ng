#include "platform/tc002/daemon/ip/IpService.h"

#include "platform/tc002/daemon/ip/IpController.h"
#include "platform/tc002/daemon/ip/SystemPlatform.h"

namespace awtrix {
namespace tc002d {

struct IpService::Impl {
  Impl(DeviceState& state, IpOptions options) : platform(options.runDir), controller(state, std::move(options), platform) {}
  ip::SystemPlatform platform;
  ip::IpController controller;
};

IpService::IpService(DeviceState& state, IpOptions options)
    : impl_(std::make_unique<Impl>(state, std::move(options))) {}

IpService::~IpService() = default;

bool IpService::start(int64_t nowMs) { return impl_->controller.start(nowMs); }

void IpService::pollInterest(std::vector<PollInterest>& out) const { impl_->controller.pollInterest(out); }

void IpService::onReady(int fd, short revents, int64_t nowMs) { impl_->controller.onReady(fd, revents, nowMs); }

int64_t IpService::nextDeadlineMs() const { return impl_->controller.nextDeadlineMs(); }

void IpService::onTime(int64_t nowMs) { impl_->controller.onTime(nowMs); }

bool IpService::onChildExit(pid_t pid, int status, int64_t nowMs) {
  return impl_->controller.onChildExit(pid, status, nowMs);
}

void IpService::requestStop(int64_t nowMs) { impl_->controller.requestStop(nowMs); }

bool IpService::stopped() const { return impl_->controller.stopped(); }

void IpService::setServer(const std::string& server) { impl_->controller.setServer(server); }

void IpService::setHostname(const std::string& hostname) { impl_->controller.setHostname(hostname); }

void IpService::setStaticAddress(const tc002::StaticAddress& address) {
  impl_->controller.setStaticAddress(address);
}

}
}
