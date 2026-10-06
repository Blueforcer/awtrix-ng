#include "platform/linux/ble/LinuxBleRadio.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "platform/linux/ble/LinuxBleProtocol.h"

namespace awtrix::ble {
using namespace linux_detail;

namespace {

// EBUSY is the controller turning the connect command down for the moment (Command Disallowed).
bool retryable(int error) {
  return error == ENOSYS || error == EIO || error == ECONNRESET || error == ECONNABORTED || error == EPROTO ||
         error == EBUSY;
}

}

bool LinuxBleRadio::openListener() {
  listenFd_ = ::socket(kAfBluetooth, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, kProtoL2cap);
  if (listenFd_ < 0) return false;
  SockaddrL2 a{};
  a.family = static_cast<sa_family_t>(kAfBluetooth);
  a.cid = kAttCid;
  a.bdaddrType = kLePublic;
  if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 || ::listen(listenFd_, 4) < 0) {
    ::close(listenFd_);
    listenFd_ = -1;
    return false;
  }
  return true;
}

void LinuxBleRadio::acceptLink() {
  SockaddrL2 peer{};
  socklen_t len = sizeof peer;
  const int fd = ::accept4(listenFd_, reinterpret_cast<sockaddr*>(&peer), &len, SOCK_CLOEXEC | SOCK_NONBLOCK);
  if (fd < 0) return;
  const Address address = fromWire(peer.bdaddr, peer.bdaddrType);
  L2capConninfo info{};
  socklen_t size = sizeof info;
  const bool discard = cancel_ && cancel_->discardSocket;
  bool haveInfo = (directLink_ || discard) && ::getsockopt(fd, kSolL2cap, kL2capConninfo, &info, &size) == 0;
  if (discard && (address.b == cancel_->peer.b || (haveInfo && cancel_->handle == info.handle))) {
    ::close(fd);
    cancel_->discardSocket = false;
    if (!cancel_->handle) finishCancel();
    return;
  }
  if (directLink_) {
    auto it = links_.find(directLink_);
    const bool handleMatches = it != links_.end() && haveInfo && it->second.handle == info.handle;
    if (it != links_.end() && (it->second.peer.b == address.b || handleMatches)) {
      const int id = directLink_;
      directLink_ = 0;
      Link& link = it->second;
      link.fd = fd;
      link.open = true;
      link.connectDeadline = -1;
      if (link.level > 1 && !secure(id, link.level)) {
        close(id, false);
        if (events_.connected) events_.connected(id, false, "cannot secure the connection");
        return;
      }
      note("ble: connected to " + link.peer.str() + " through the ATT listener");
      resume();
      if (events_.connected) events_.connected(id, true, "");
      return;
    }
  }
  const int id = nextLink_++;
  Link& link = links_[id];
  link.fd = fd;
  link.peer = address;
  link.open = true;
  if (!haveInfo) {
    size = sizeof info;
    haveInfo = ::getsockopt(fd, kSolL2cap, kL2capConninfo, &info, &size) == 0;
  }
  link.peripheral = !haveInfo || !peripheralHandles_.count(info.handle) || peripheralHandles_[info.handle];
  note("ble: " + link.peer.str() + " connected to us");
  resume();
  if (events_.accepted) events_.accepted(id, link.peer);
}

int LinuxBleRadio::connect(const Address& peer, int securityLevel) {
  if (state_ != State::On) return -1;
  const int id = nextLink_++;
  Link& link = links_[id];
  link.peer = peer;
  link.outgoing = true;
  link.level = securityLevel;
  link.retryAt = now_;
  connectQueue_.push_back(id);
  // New requests join the queue even between dispatch and tick. Only its oldest eligible
  // request may start, and waiting consumes neither a socket nor a connection attempt.
  if (connecting() || nextQueuedConnection() != id) return id;
  dequeueConnection(id);
  if (!startConnect(id, link)) {
    const std::string why = std::strerror(errno);
    links_.erase(id);
    resume();
    note("ble: cannot connect to " + peer.str() + ": " + why);
    return -1;
  }
  return id;
}

int LinuxBleRadio::nextQueuedConnection() const {
  for (int id : connectQueue_) {
    const auto it = links_.find(id);
    if (it != links_.end() && now_ >= it->second.retryAt) return id;
  }
  return 0;
}

void LinuxBleRadio::dequeueConnection(int id) {
  connectQueue_.erase(std::remove(connectQueue_.begin(), connectQueue_.end(), id), connectQueue_.end());
}

bool LinuxBleRadio::startConnect(int id, Link& link) {
  ++link.attempts;
  link.retryAt = -1;
  // 4.9 refuses its L2CAP scan-to-connect path while a peripheral link exists.
  // Initiate first; the kernel then exposes the regular ATT socket through our listener.
  if (link.attempts == 1) link.direct = legacyKernel_ && incoming();
  if (link.direct) {
    Bytes params;
    link.handle.reset();
    put16(params, 0x0060);
    put16(params, 0x0030);
    params.push_back(0);
    params.push_back(link.peer.random ? 1 : 0);
    params.insert(params.end(), link.peer.b.begin(), link.peer.b.end());
    params.push_back(0);
    put16(params, 0x0028);
    put16(params, 0x0038);
    put16(params, 0);
    put16(params, 0x002a);
    put16(params, 0);
    put16(params, 0);
    if (!hciCommand(0x200a, {0x00}) || !hciCommand(0x200c, {0x00, 0x00}) || !hciCommand(0x200d, params)) return false;
    directLink_ = id;
    link.connectDeadline = now_ + kDirectConnectMs;
    note("ble: initiating " + link.peer.str() + " alongside a peripheral link");
    return true;
  }
  link.fd = ::socket(kAfBluetooth, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, kProtoL2cap);
  if (link.fd < 0) return false;
  const auto failed = [&link] {
    const int error = errno;
    ::close(link.fd);
    link.fd = -1;
    errno = error;
    return false;
  };
  SockaddrL2 local{};
  local.family = static_cast<sa_family_t>(kAfBluetooth);
  local.cid = kAttCid;
  local.bdaddrType = kLePublic;
  if (::bind(link.fd, reinterpret_cast<sockaddr*>(&local), sizeof local) < 0) return failed();
  if (link.level > 1) {
    BtSecurity s{static_cast<uint8_t>(link.level), 0};
    ::setsockopt(link.fd, kSolBluetooth, kBtSecurity, &s, sizeof s);
  }
  SockaddrL2 remote{};
  remote.family = static_cast<sa_family_t>(kAfBluetooth);
  remote.cid = kAttCid;
  remote.bdaddrType = link.peer.random ? kLeRandom : kLePublic;
  std::copy(link.peer.b.begin(), link.peer.b.end(), remote.bdaddr);
  if (::connect(link.fd, reinterpret_cast<sockaddr*>(&remote), sizeof remote) == 0 || errno == EINPROGRESS) return true;
  return failed();
}

void LinuxBleRadio::failDirect(const std::string& why) {
  const int id = directLink_;
  directLink_ = 0;
  auto it = links_.find(id);
  if (it == links_.end()) return;
  Link& link = it->second;
  link.connectDeadline = -1;
  note("ble: connecting to " + link.peer.str() + ": " + why);
  if (link.attempts < options_.connectAttempts) {
    link.retryAt = now_ + options_.retryDelayMs;
    connectQueue_.push_back(id);
    return;
  }
  links_.erase(it);
  resume();
  if (events_.connected) events_.connected(id, false, why);
}

// The caller drops `link` afterwards. A command that cannot be sent gets no answer to wait for.
void LinuxBleRadio::cancelDirect(const Link& link) {
  directLink_ = 0;
  cancel_ = DirectCancel{link.peer, link.handle, true, false, now_ + kCancelMs};
  if (!(link.handle ? sendDisconnect(*link.handle) : hciCommand(0x200e, {}))) finishCancel();
}

bool LinuxBleRadio::sendDisconnect(uint16_t handle) {
  Bytes params;
  put16(params, handle);
  params.push_back(0x13);
  return hciCommand(0x0406, params);
}

void LinuxBleRadio::finishCancel() {
  cancel_.reset();
  resume();
}

// A connection that dies before its first exchange is tried again.
void LinuxBleRadio::finishConnect(int id, Link& link) {
  int error = 0;
  socklen_t len = sizeof error;
  if (::getsockopt(link.fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) error = errno;
  if (error == 0) {
    link.open = true;
    note("ble: connected to " + link.peer.str() + " after " + std::to_string(link.attempts) + " attempt(s)");
    resume();
    if (events_.connected) events_.connected(id, true, "");
    return;
  }
  ::close(link.fd);
  link.fd = -1;
  if (retryable(error) && link.attempts < options_.connectAttempts) {
    link.retryAt = now_ + options_.retryDelayMs;
    connectQueue_.push_back(id);
    return;
  }
  const std::string why = error == ETIMEDOUT || error == EHOSTDOWN ? "device not found" : std::strerror(error);
  note("ble: connecting to " + link.peer.str() + " failed after " + std::to_string(link.attempts) + " attempt(s): " + why);
  links_.erase(id);
  resume();
  if (events_.connected) events_.connected(id, false, why);
}

void LinuxBleRadio::readLink(int id, Link& link) {
  uint8_t buf[600];
  for (;;) {
    const ssize_t n = ::recv(link.fd, buf, sizeof buf, MSG_DONTWAIT);
    if (n > 0) {
      if (events_.att) events_.att(id, buf, static_cast<std::size_t>(n));
      if (!links_.count(id)) return;
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
    close(id, true);
    return;
  }
}

void LinuxBleRadio::flush(Link& link) {
  while (!link.out.empty()) {
    const Bytes& pdu = link.out.front();
    if (::send(link.fd, pdu.data(), pdu.size(), MSG_DONTWAIT | MSG_NOSIGNAL) < 0) return;
    link.out.pop_front();
  }
}

void LinuxBleRadio::close(int id, bool notify) {
  auto it = links_.find(id);
  if (it == links_.end()) return;
  if (directLink_ == id) cancelDirect(it->second);
  if (it->second.fd >= 0) ::close(it->second.fd);
  links_.erase(it);
  dequeueConnection(id);
  resume();
  if (notify && events_.disconnected) events_.disconnected(id);
}

void LinuxBleRadio::disconnect(int link) { close(link, false); }

bool LinuxBleRadio::send(int id, const Bytes& pdu) {
  auto it = links_.find(id);
  if (it == links_.end() || !it->second.open) return false;
  Link& link = it->second;
  if (link.out.empty() && ::send(link.fd, pdu.data(), pdu.size(), MSG_DONTWAIT | MSG_NOSIGNAL) >= 0) return true;
  if (!link.out.empty() || errno == EAGAIN) {
    if (link.out.size() >= kMaxQueued) return false;
    link.out.push_back(pdu);
    return true;
  }
  return false;
}

int LinuxBleRadio::security(int id) const {
  auto it = links_.find(id);
  if (it == links_.end() || it->second.fd < 0) return 0;
  BtSecurity s{};
  socklen_t len = sizeof s;
  if (::getsockopt(it->second.fd, kSolBluetooth, kBtSecurity, &s, &len) < 0) return 1;
  return s.level;
}

bool LinuxBleRadio::secure(int id, int level) {
  auto it = links_.find(id);
  if (it == links_.end() || !it->second.open) return false;
  BtSecurity s{static_cast<uint8_t>(level), 0};
  if (::setsockopt(it->second.fd, kSolBluetooth, kBtSecurity, &s, sizeof s) < 0) return false;
  it->second.secureTarget = level;
  it->second.secureDeadline = now_ + options_.secureTimeoutMs;
  return true;
}

Address LinuxBleRadio::peer(int id) const {
  auto it = links_.find(id);
  return it == links_.end() ? Address() : it->second.peer;
}

}  // namespace awtrix::ble
