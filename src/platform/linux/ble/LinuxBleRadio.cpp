#include "platform/linux/ble/LinuxBleRadio.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/posix/Files.h"
#include "platform/linux/ble/LinuxBleProtocol.h"

namespace awtrix::ble {
using namespace linux_detail;
LinuxBleRadio::LinuxBleRadio(Options options) : options_(std::move(options)), bonds_(options_.bondsPath, options_.log) {
  utsname kernel{};
  legacyKernel_ = ::uname(&kernel) == 0 && std::strncmp(kernel.release, "4.9.", 4) == 0;
  bonds_.load();
}

LinuxBleRadio::~LinuxBleRadio() {
  if (const auto it = links_.find(directLink_); it != links_.end()) cancelDirect(it->second);
  for (auto& [id, link] : links_)
    if (link.fd >= 0) ::close(link.fd);
  links_.clear();
  if (scanFd_ >= 0) ::close(scanFd_);
  if (listenFd_ >= 0) ::close(listenFd_);
}

void LinuxBleRadio::note(const std::string& text) const {
  if (options_.log) options_.log(text);
}

bool LinuxBleRadio::advertise(int instance, bool connectable, const Bytes& adv, const Bytes& scanResponse,
                              std::string& error) {
  if (state_ != State::On) {
    error = "Bluetooth is off";
    return false;
  }
  Bytes params{static_cast<uint8_t>(instance)};
  const uint32_t flags = (connectable ? 0x1u : 0u) | 0x2u;
  for (int i = 0; i < 4; ++i) params.push_back(static_cast<uint8_t>(flags >> (8 * i)));
  put16(params, 0);
  put16(params, 0);
  params.push_back(static_cast<uint8_t>(adv.size()));
  params.push_back(static_cast<uint8_t>(scanResponse.size()));
  params.insert(params.end(), adv.begin(), adv.end());
  params.insert(params.end(), scanResponse.begin(), scanResponse.end());
  const int status = mgmt(kAddAdvertising, params);
  if (status != 0) {
    error = "the controller refused the advert (status " + std::to_string(status) + ")";
    return false;
  }
  adverts_[instance] = {connectable, !scanResponse.empty()};
  if (legacyKernel_ && !links_.empty()) restoreAdvertising();
  return true;
}

void LinuxBleRadio::stopAdvertising(int instance) {
  if (!adverts_.erase(instance)) return;
  mgmt(kRemoveAdvertising, {static_cast<uint8_t>(instance)});
}

std::vector<Address> LinuxBleRadio::bonds() const { return bonds_.peers(); }

bool LinuxBleRadio::forget(const Address& who) {
  if (!bonds_.contains(who)) return false;
  if (state_ == State::On) {
    Bytes params(who.b.begin(), who.b.end());
    params.push_back(who.random ? kLeRandom : kLePublic);
    params.push_back(0x01);
    mgmt(kUnpairDevice, params);
  }
  bonds_.forget(who);
  return true;
}

void LinuxBleRadio::collect(std::vector<pollfd>& out) const {
  if (management_.fd() >= 0) out.push_back({management_.fd(), POLLIN, 0});
  if (scanFd_ >= 0) out.push_back({scanFd_, POLLIN, 0});
  if (listenFd_ >= 0) out.push_back({listenFd_, POLLIN, 0});
  for (const auto& [id, link] : links_) {
    if (link.fd < 0) continue;
    short events = link.open ? POLLIN : POLLOUT;
    if (link.open && !link.out.empty()) events |= POLLOUT;
    out.push_back({link.fd, events, 0});
  }
}

void LinuxBleRadio::dispatch(const std::vector<pollfd>& ready, int64_t nowMs) {
  now_ = nowMs;
  for (const pollfd& p : ready) {
    if (!p.revents) continue;
    if (p.fd == management_.fd()) {
      readMgmt();
      continue;
    }
    if (p.fd == scanFd_) {
      readScanner();
      continue;
    }
    if (p.fd == listenFd_) {
      acceptLink();
      continue;
    }
    for (auto it = links_.begin(); it != links_.end(); ++it) {
      if (it->second.fd != p.fd) continue;
      const int id = it->first;
      Link& link = it->second;
      if (!link.open) {
        finishConnect(id, link);
      } else {
        if (p.revents & POLLOUT) flush(link);
        if (p.revents & (POLLIN | POLLHUP | POLLERR)) readLink(id, link);
      }
      break;
    }
  }
  if (management_.hasEvents()) readMgmt();
}

void LinuxBleRadio::tick(int64_t nowMs) {
  now_ = nowMs;
  if (state_ == State::Waiting && waitDeadline_ >= 0 && nowMs >= waitDeadline_) fail("the Bluetooth controller did not appear");
  if (scanWanted_ && nowMs - lastReport_ > kScanQuietMs) applyScan();
  if (directLink_) {
    auto it = links_.find(directLink_);
    if (it != links_.end() && nowMs >= it->second.connectDeadline) {
      const int id = directLink_;
      cancelDirect(it->second);
      links_.erase(id);
      if (events_.connected) events_.connected(id, false, "device not found");
    }
  }
  // A controller that never answers must not hold up every later connection: a disconnect is
  // asked for once more, then the cancellation counts as done either way.
  if (cancel_ && nowMs >= cancel_->deadline) {
    if (cancel_->handle && !cancel_->resent && sendDisconnect(*cancel_->handle)) {
      cancel_->resent = true;
      cancel_->deadline = nowMs + kCancelMs;
    } else {
      note("ble: the controller did not confirm the cancelled connection");
      finishCancel();
    }
  }
  std::vector<int> secured, expired;
  for (auto& [id, link] : links_) {
    if (link.secureTarget && link.open) {
      if (security(id) >= link.secureTarget) secured.push_back(id);
      else if (nowMs >= link.secureDeadline) expired.push_back(id);
    }
  }
  while (!connecting()) {
    const int id = nextQueuedConnection();
    if (!id) break;
    dequeueConnection(id);
    auto it = links_.find(id);
    if (it == links_.end()) continue;
    Link& link = it->second;
    if (!startConnect(id, link)) {
      const std::string why = std::strerror(errno);
      links_.erase(id);
      resume();
      if (events_.connected) events_.connected(id, false, why);
    }
  }
  for (int id : secured) {
    auto it = links_.find(id);
    if (it == links_.end()) continue;
    it->second.secureTarget = 0;
    if (events_.secured) events_.secured(id, true, "");
  }
  for (int id : expired) {
    auto it = links_.find(id);
    if (it == links_.end()) continue;
    it->second.secureTarget = 0;
    if (events_.secured) events_.secured(id, false, "pairing timed out");
  }
}

}
