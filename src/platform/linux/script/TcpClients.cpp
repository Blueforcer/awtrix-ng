#include "platform/posix/Files.h"
#include "platform/linux/script/TcpClients.h"

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <system_error>

namespace awtrix::linux_script {
namespace {

void signal(int fd) {
  const uint64_t one = 1;
  while (::write(fd, &one, sizeof one) < 0 && errno == EINTR) {
  }
}

const char* reasonFor(int error) {
  if (error == ECONNREFUSED) return "refused";
  if (error == ETIMEDOUT) return "timeout";
  return "error";
}

}

std::vector<TcpAddress> TcpClients::systemLookup(const std::string& host, uint16_t port) {
  return posix::Resolver::lookup(host, std::to_string(port), kAddresses);
}

TcpClients::TcpClients(Lookup lookup) : lookup_(std::move(lookup)) {
  wake_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (wake_ < 0) throw std::system_error(errno, std::generic_category(), "tcp wake");
  try {
    worker_ = std::thread([this] { run(); });
  } catch (...) {
    ::close(wake_);
    throw;
  }
}

TcpClients::~TcpClients() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
  }
  wake();
  worker_.join();
  for (auto& kv : conns_)
    if (kv.second.fd >= 0) ::close(kv.second.fd);
  ::close(wake_);
}

void TcpClients::wake() { signal(wake_); }

uint32_t TcpClients::connect(const std::string& app, const std::string& host, uint16_t port, int timeoutMs) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (app.empty() || host.empty() || host.size() > 253 || host.find('\0') != std::string::npos || !port) return 0;
  std::size_t mine = 0, open = 0;
  for (const auto& kv : conns_) {
    if (kv.second.dropped) continue;
    ++open;
    if (kv.second.app == app) ++mine;
  }
  if (mine >= kPerApp || open >= kTotal) return 0;
  while (!next_ || next_ > 0x7fffffffu || conns_.count(next_)) {
    if (!next_ || next_ > 0x7fffffffu) next_ = 1;
    else ++next_;
  }
  const uint32_t id = next_++;
  Conn c;
  c.app = app;
  c.host = host;
  c.port = port;
  c.deadline = posix::monotonicMs() + std::clamp(timeoutMs, 1, 60000);
  conns_.emplace(id, std::move(c));
  wake();
  return id;
}

bool TcpClients::send(const std::string& app, uint32_t id, const std::string& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = conns_.find(id);
  if (it == conns_.end() || it->second.dropped || it->second.state == State::Closed || it->second.app != app) return false;
  if (data.size() > kOutMax - it->second.out.size()) return false;
  it->second.out += data;
  wake();
  return true;
}

void TcpClients::close(const std::string& app, uint32_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = conns_.find(id);
  if (it == conns_.end() || it->second.app != app) return;
  it->second.dropped = true;
  it->second.events.clear();
  it->second.queued = 0;
  wake();
}

void TcpClients::forget(const std::string& app) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& kv : conns_) {
    if (kv.second.app != app) continue;
    kv.second.dropped = true;
    kv.second.events.clear();
    kv.second.queued = 0;
  }
  wake();
}

// Takes the next event from the connection after the one served last, so no connection's backlog
// holds up another's.
bool TcpClients::pop(TcpEvent& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = conns_.upper_bound(cursor_);
  for (std::size_t n = 0; n < conns_.size(); ++n, ++it) {
    if (it == conns_.end()) it = conns_.begin();
    Conn& c = it->second;
    if (c.dropped || c.events.empty()) continue;
    out = std::move(c.events.front());
    c.events.pop_front();
    if (out.kind == TcpEvent::Kind::Line) c.queued -= out.data.size() + kEventCost;
    cursor_ = it->first;
    // A closed connection keeps its quota until its last event is taken.
    if (out.kind == TcpEvent::Kind::Closed) conns_.erase(it);
    return true;
  }
  return false;
}

void TcpClients::emit(Conn& c, uint32_t id, TcpEvent::Kind kind, std::string data) {
  if (kind == TcpEvent::Kind::Line) c.queued += data.size() + kEventCost;
  c.events.push_back({c.app, id, kind, std::move(data)});
}

void TcpClients::finish(uint32_t id, Conn& c, const char* reason) {
  if (c.fd >= 0) ::close(c.fd);
  c.fd = -1;
  if (!c.dropped && c.state != State::Closed) emit(c, id, TcpEvent::Kind::Closed, reason);
  c.state = State::Closed;
  c.in.clear();
  c.out.clear();
  c.addresses.clear();
  c.lookup.reset();
}

void TcpClients::reap() {
  for (auto it = conns_.begin(); it != conns_.end();) {
    if (it->second.dropped) {
      if (it->second.fd >= 0) ::close(it->second.fd);
      it = conns_.erase(it);
    } else {
      ++it;
    }
  }
}

void TcpClients::startLookups() {
  for (auto& kv : conns_) {
    Conn& c = kv.second;
    if (c.dropped || c.state != State::Resolve || c.lookup) continue;
    c.lookup = posix::Resolver::start([lookup = lookup_, host = c.host, port = c.port] {
      auto addresses = lookup(host, port);
      if (addresses.size() > kAddresses) addresses.resize(kAddresses);
      return addresses;
    });
    if (!c.lookup) finish(kv.first, c, "error");
  }
}

void TcpClients::takeAnswers() {
  for (auto& kv : conns_) {
    Conn& c = kv.second;
    if (c.dropped || c.state != State::Resolve || !c.lookup || !c.lookup->take(c.addresses)) continue;
    c.lookup.reset();
    if (c.addresses.empty()) finish(kv.first, c, "dns");
    else connectNext(kv.first, c);
  }
}

void TcpClients::expire(int64_t now) {
  for (auto& kv : conns_) {
    Conn& c = kv.second;
    if (c.dropped || now < c.deadline) continue;
    if (c.state == State::Resolve || c.state == State::Connecting) finish(kv.first, c, "timeout");
  }
}

void TcpClients::connectNext(uint32_t id, Conn& c) {
  if (c.fd >= 0) ::close(c.fd);
  c.fd = -1;
  if (posix::monotonicMs() >= c.deadline) return finish(id, c, "timeout");
  auto result = posix::connectNext(c.addresses, c.nextAddress);
  c.fd = result.fd.release();
  if (result.state != posix::ConnectState::Failed) {
    c.state = result.state == posix::ConnectState::Connected ? State::Open : State::Connecting;
    if (c.state == State::Open) emit(c, id, TcpEvent::Kind::Open, "");
    return;
  }
  if (result.error) c.lastError = result.error;
  finish(id, c, reasonFor(c.lastError));
}

void TcpClients::readFrom(uint32_t id, Conn& c) {
  char buf[4096];
  for (int budget = 0; budget < 4; ++budget) {
    const ssize_t n = ::read(c.fd, buf, sizeof buf);
    if (n == 0) return finish(id, c, "closed");
    if (n < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      return finish(id, c, reasonFor(errno));
    }
    for (ssize_t i = 0; i < n; ++i) {
      const char ch = buf[i];
      if (ch == '\0') return finish(id, c, "error");
      if (ch == '\n') {
        if (!c.in.empty() && c.in.back() == '\r') c.in.pop_back();
        if (c.queued + c.in.size() + kEventCost > kInMax) return finish(id, c, "overflow");
        emit(c, id, TcpEvent::Kind::Line, std::move(c.in));
        c.in.clear();
      } else {
        if (c.in.size() >= kLineMax) return finish(id, c, "overflow");
        c.in += ch;
      }
    }
  }
}

void TcpClients::writeTo(uint32_t id, Conn& c) {
  if (c.out.empty()) return;
  const ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
    return finish(id, c, reasonFor(errno));
  }
  if (n == 0) return finish(id, c, "closed");
  c.out.erase(0, static_cast<size_t>(n));
}

void TcpClients::run() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!quit_) {
    reap();
    takeAnswers();
    startLookups();

    std::vector<pollfd> fds{{wake_, POLLIN, 0}};
    std::vector<uint32_t> ids{0};
    int64_t soonest = posix::monotonicMs() + 1000;
    for (auto& kv : conns_) {
      Conn& c = kv.second;
      if (c.dropped) continue;
      if (c.state == State::Resolve || c.state == State::Connecting) soonest = std::min(soonest, c.deadline);
      if (c.state == State::Resolve && c.lookup) {
        fds.push_back({c.lookup->fd(), POLLIN, 0});
        ids.push_back(kv.first);
      }
      if (c.fd < 0) continue;
      short want = POLLIN;
      if (c.state == State::Connecting) want = POLLOUT;
      else if (!c.out.empty()) want |= POLLOUT;
      fds.push_back({c.fd, want, 0});
      ids.push_back(kv.first);
    }
    const int wait = static_cast<int>(std::max<int64_t>(0, soonest - posix::monotonicMs()));
    lock.unlock();
    ::poll(fds.data(), fds.size(), wait);
    lock.lock();
    uint64_t drained;
    while (::read(wake_, &drained, sizeof drained) > 0) {
    }
    for (std::size_t i = 1; i < fds.size(); ++i) {
      auto it = conns_.find(ids[i]);
      if (it == conns_.end() || it->second.dropped || it->second.fd != fds[i].fd || !fds[i].revents) continue;
      Conn& c = it->second;
      if (c.state == State::Connecting) {
        int error = 0;
        socklen_t length = sizeof error;
        if (getsockopt(c.fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0) error = errno;
        if (error != 0) {
          c.lastError = error;
          connectNext(ids[i], c);
          continue;
        }
        c.state = State::Open;
        emit(c, ids[i], TcpEvent::Kind::Open, "");
      }
      if (c.state != State::Open) continue;
      if (fds[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) readFrom(ids[i], c);
      if (c.state == State::Open && (fds[i].revents & POLLOUT)) writeTo(ids[i], c);
    }
    expire(posix::monotonicMs());
  }
}

}
