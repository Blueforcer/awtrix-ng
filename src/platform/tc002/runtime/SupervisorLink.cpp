#include "platform/tc002/runtime/SupervisorLink.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

namespace awtrix {
namespace {
void erase(std::string& text) {
  if (!text.empty()) ::explicit_bzero(&text[0], text.size());
  text.clear();
}
}

SupervisorLink::~SupervisorLink() {
  lose();
  if (fd_ >= 0) ::close(fd_);
}

bool validateSupervisorSocket(int descriptor, std::string& error) {
  struct stat info{};
  int type = 0, domain = 0;
  socklen_t length = sizeof(type);
  if (::fstat(descriptor, &info) != 0 || !S_ISSOCK(info.st_mode) ||
      ::getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &type, &length) != 0 || type != SOCK_SEQPACKET) {
    error = "descriptor is not a SOCK_SEQPACKET socket"; return false;
  }
  length = sizeof(domain);
  if (::getsockopt(descriptor, SOL_SOCKET, SO_DOMAIN, &domain, &length) != 0 || domain != AF_UNIX) {
    error = "descriptor is not a local socket"; return false;
  }
  ucred peer{};
  length = sizeof(peer);
  if (::getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &peer, &length) != 0 || peer.pid <= 0 ||
      (peer.uid != 0 && peer.uid != ::geteuid())) {
    error = "descriptor is not connected to a supervisor of this user"; return false;
  }
  return true;
}

bool SupervisorLink::open(int descriptor, std::string& error) {
  if (fd_ >= 0) { error = "supervisor channel already open"; return false; }
  if (!validateSupervisorSocket(descriptor, error)) return false;
  const int flags = ::fcntl(descriptor, F_GETFL);
  if (flags < 0 || ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0 ||
      ::fcntl(descriptor, F_SETFD, FD_CLOEXEC) != 0) {
    error = std::string("cannot configure descriptor: ") + std::strerror(errno); return false;
  }
  fd_ = descriptor;
  closed_ = false;
  return true;
}

void SupervisorLink::lose() {
  closed_ = true;
  for (auto& datagram : queue_) erase(datagram);
  queue_.clear();
}

void SupervisorLink::transmit() {
  while (fd_ >= 0 && !closed_ && !queue_.empty()) {
    std::string& next = queue_.front();
    const ssize_t sent = ::send(fd_, next.data(), next.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS) return;
      lose();
      return;
    }
    erase(next);
    queue_.pop_front();
  }
}

bool SupervisorLink::send(std::string datagram) {
  if (fd_ < 0 || closed_ || datagram.empty() || datagram.size() > tc002::kMaxSupervisorMessage ||
      queue_.size() >= kMaxQueued) {
    erase(datagram);
    return false;
  }
  queue_.push_back(std::move(datagram));
  transmit();
  return !closed_;
}

bool SupervisorLink::poll(const std::function<void(const tc002::SupervisorMessage&)>& onMessage) {
  if (fd_ < 0) return true;
  if (closed_) return false;
  char buffer[tc002::kMaxSupervisorMessage + 1];
  for (unsigned count = 0; count < kMaxReceivedPerPoll; ++count) {
    iovec part{buffer, sizeof(buffer)};
    msghdr header{};
    header.msg_iov = &part;
    header.msg_iovlen = 1;
    const ssize_t received = ::recvmsg(fd_, &header, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (received < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      lose();
      return false;
    }
    if (received == 0) { lose(); return false; }
    tc002::SupervisorMessage message;
    if ((header.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) ||
        !tc002::decodeSupervisorMessage(std::string_view(buffer, static_cast<size_t>(received)), message)) {
      ++refused_;
      continue;
    }
    onMessage(message);
  }
  transmit();
  return !closed_;
}

bool SupervisorLink::flush(int timeoutMs) {
  using Clock = std::chrono::steady_clock;
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
  transmit();
  while (fd_ >= 0 && !closed_ && !queue_.empty()) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    if (remaining <= 0) return false;
    pollfd entry{fd_, POLLOUT, 0};
    if (::poll(&entry, 1, static_cast<int>(remaining)) < 0 && errno != EINTR) return false;
    transmit();
  }
  return fd_ >= 0 && !closed_ && queue_.empty();
}

}
