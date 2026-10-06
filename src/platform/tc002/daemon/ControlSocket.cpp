#include "platform/tc002/daemon/ControlSocket.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "core/api/JsonWriter.h"
#include "platform/tc002/daemon/Log.h"

namespace awtrix {
namespace tc002d {
namespace {

std::string errorReply(std::string_view message) {
  std::string out;
  api::JsonWriter json(out);
  json.beginObject().member("ok", false).member("error", message).endObject();
  return out;
}

bool socketAddress(const std::string& path, sockaddr_un& address) {
  address = sockaddr_un{};
  address.sun_family = AF_UNIX;
  if (path.empty() || path.size() >= sizeof address.sun_path) {
    errno = ENAMETOOLONG;
    return false;
  }
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  return true;
}

uint32_t frameLength(const std::string& buffer) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(buffer[0])) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(buffer[1])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(buffer[2])) << 8) | static_cast<uint8_t>(buffer[3]);
}

}

std::string encodeControlFrame(std::string_view payload) {
  std::string out;
  const auto size = static_cast<uint32_t>(payload.size());
  out.push_back(static_cast<char>(size >> 24));
  out.push_back(static_cast<char>(size >> 16));
  out.push_back(static_cast<char>(size >> 8));
  out.push_back(static_cast<char>(size));
  out.append(payload.data(), payload.size());
  return out;
}

ControlSocket::ControlSocket(ControlOptions options) : options_(std::move(options)) {}

ControlSocket::~ControlSocket() { closeListener(); }

void ControlSocket::add(std::string command, Handler handler) {
  for (auto& entry : handlers_) {
    if (entry.first == command) {
      entry.second = std::move(handler);
      return;
    }
  }
  handlers_.emplace_back(std::move(command), std::move(handler));
}

std::string ControlSocket::dispatch(std::string_view request) const {
  const auto newline = request.find('\n');
  std::string command = posix::trimmed(request.substr(0, newline));
  const std::string_view payload = newline == std::string_view::npos ? std::string_view() : request.substr(newline + 1);
  if (command == "commands") {
    std::string out;
    api::JsonWriter json(out);
    json.beginArray().value("commands");
    for (const auto& entry : handlers_) json.value(entry.first);
    json.endArray();
    return out;
  }
  for (const auto& entry : handlers_) {
    if (entry.first != command) continue;
    try {
      return entry.second(payload);
    } catch (...) {
      Log::line("control", "command %s failed with an exception", command.c_str());
      return errorReply("internal error");
    }
  }
  return errorReply("unknown command: " + command);
}

bool ControlSocket::start(int64_t nowMs) {
  stopping_ = false;
  bindListener(nowMs);
  return true;
}

bool ControlSocket::bindListener(int64_t nowMs) {
  sockaddr_un address;
  struct stat existing{};
  const char* failure = nullptr;
  posix::UniqueFd fd;
  if (!socketAddress(options_.path, address)) failure = "path";
  else if (::lstat(options_.path.c_str(), &existing) == 0 &&
           (!S_ISSOCK(existing.st_mode) || ::unlink(options_.path.c_str()) < 0)) {
    if (!S_ISSOCK(existing.st_mode)) errno = EEXIST;
    failure = "replace existing path";
  } else {
    fd.reset(::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (!fd.valid()) failure = "socket";
    else if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) < 0) failure = "bind";
    else if (::chmod(options_.path.c_str(), 0600) < 0) failure = "chmod";
    else if (::listen(fd.get(), 8) < 0) failure = "listen";
  }
  if (failure) {
    if (bindFailures_++ % 12 == 0)
      Log::line("control", "cannot listen on %s (%s: %s)", options_.path.c_str(), failure, std::strerror(errno));
    retryAt_ = nowMs + options_.bindRetryMs;
    return false;
  }
  listener_ = std::move(fd);
  retryAt_ = -1;
  bindFailures_ = 0;
  Log::line("control", "listening on %s", options_.path.c_str());
  return true;
}

void ControlSocket::closeListener() {
  if (!listener_.valid()) return;
  listener_.reset();
  ::unlink(options_.path.c_str());
}

void ControlSocket::pollInterest(std::vector<PollInterest>& out) const {
  if (listener_.valid() && clients_.size() < options_.maxClients) out.push_back({listener_.get(), POLLIN});
  for (const auto& client : clients_)
    out.push_back({client->fd.get(), static_cast<short>(client->output.empty() ? POLLIN : POLLOUT)});
}

void ControlSocket::acceptClients(int64_t nowMs) {
  while (clients_.size() < options_.maxClients) {
    posix::UniqueFd fd(::accept4(listener_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
    if (!fd.valid()) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && errno != ECONNABORTED)
        Log::line("control", "accept failed: %s", std::strerror(errno));
      return;
    }
    auto client = std::make_unique<Client>();
    client->fd = std::move(fd);
    client->deadline = nowMs + options_.clientTimeoutMs;
    ucred peer{};
    socklen_t length = sizeof peer;
    if (::getsockopt(client->fd.get(), SOL_SOCKET, SO_PEERCRED, &peer, &length) < 0 || peer.uid != options_.peerUid) {
      Log::line("control", "refused peer uid %d pid %d", static_cast<int>(peer.uid), static_cast<int>(peer.pid));
      client->refused = true;
    }
    clients_.push_back(std::move(client));
  }
}

void ControlSocket::serviceClient(Client& client, short revents) {
  if (client.output.empty() && (revents & (POLLIN | POLLHUP | POLLERR))) {
    char buffer[1024];
    for (;;) {
      const ssize_t count = ::recv(client.fd.get(), buffer, sizeof buffer, MSG_DONTWAIT);
      if (count < 0 && errno == EINTR) continue;
      if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
      if (count <= 0) {
        client.fd.reset();
        return;
      }
      client.input.append(buffer, static_cast<std::size_t>(count));
      if (client.input.size() >= 4) {
        const uint32_t length = frameLength(client.input);
        if (length > kMaxRequest) {
          client.output = encodeControlFrame(errorReply("request too large"));
          break;
        }
        if (client.input.size() >= 4 + static_cast<std::size_t>(length)) {
          const std::string_view request(client.input.data() + 4, length);
          std::string reply = client.refused ? errorReply("permission denied") : dispatch(request);
          if (reply.size() > kMaxReply) reply = errorReply("reply too large");
          client.output = encodeControlFrame(reply);
          break;
        }
      }
    }
  }
  if (client.output.empty()) return;
  while (client.sent < client.output.size()) {
    const ssize_t count = ::send(client.fd.get(), client.output.data() + client.sent,
                                 client.output.size() - client.sent, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    if (count <= 0) break;
    client.sent += static_cast<std::size_t>(count);
  }
  client.fd.reset();
}

void ControlSocket::onReady(int fd, short revents, int64_t nowMs) {
  if (listener_.valid() && fd == listener_.get()) {
    acceptClients(nowMs);
    return;
  }
  for (auto& client : clients_) {
    if (client->fd.get() == fd) {
      serviceClient(*client, revents);
      break;
    }
  }
  clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const auto& c) { return !c->fd.valid(); }),
                 clients_.end());
}

int64_t ControlSocket::nextDeadlineMs() const {
  int64_t due = listener_.valid() || stopping_ ? -1 : retryAt_;
  for (const auto& client : clients_)
    if (due < 0 || client->deadline < due) due = client->deadline;
  return due;
}

void ControlSocket::onTime(int64_t nowMs) {
  if (!stopping_ && !listener_.valid() && retryAt_ >= 0 && nowMs >= retryAt_) bindListener(nowMs);
  for (auto& client : clients_)
    if (nowMs >= client->deadline) client->fd.reset();
  clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const auto& c) { return !c->fd.valid(); }),
                 clients_.end());
}

void ControlSocket::requestStop(int64_t nowMs) {
  (void)nowMs;
  stopping_ = true;
  retryAt_ = -1;
  closeListener();
  clients_.clear();
}

bool controlRequest(const std::string& path, std::string_view request, std::string& reply, int timeoutMs,
                    std::string& error) {
  reply.clear();
  if (request.size() > ControlSocket::kMaxRequest) {
    error = "request too large";
    return false;
  }
  sockaddr_un address;
  if (!socketAddress(path, address)) {
    error = "socket path too long";
    return false;
  }
  posix::UniqueFd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd.valid() || ::connect(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) < 0) {
    error = std::string("connect ") + path + ": " + std::strerror(errno);
    return false;
  }
  const int64_t deadline = posix::monotonicMs() + timeoutMs;
  const auto wait = [&](short events) {
    for (;;) {
      const int64_t left = deadline - posix::monotonicMs();
      if (left <= 0) {
        errno = ETIMEDOUT;
        return false;
      }
      pollfd item{fd.get(), events, 0};
      const int ready = ::poll(&item, 1, static_cast<int>(left));
      if (ready < 0 && errno == EINTR) continue;
      if (ready < 0) return false;
      if (ready > 0) return true;
    }
  };
  const std::string frame = encodeControlFrame(request);
  std::size_t sent = 0;
  while (sent < frame.size()) {
    if (!wait(POLLOUT)) {
      error = std::string("send: ") + std::strerror(errno);
      return false;
    }
    const ssize_t count = ::send(fd.get(), frame.data() + sent, frame.size() - sent, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (count <= 0) {
      error = std::string("send: ") + std::strerror(errno);
      return false;
    }
    sent += static_cast<std::size_t>(count);
  }
  std::string input;
  for (;;) {
    if (input.size() >= 4) {
      const uint32_t length = frameLength(input);
      if (length > ControlSocket::kMaxReply) {
        error = "reply too large";
        return false;
      }
      if (input.size() >= 4 + static_cast<std::size_t>(length)) {
        reply.assign(input, 4, length);
        return true;
      }
    }
    if (!wait(POLLIN)) {
      error = std::string("receive: ") + std::strerror(errno);
      return false;
    }
    char buffer[4096];
    const ssize_t count = ::recv(fd.get(), buffer, sizeof buffer, MSG_DONTWAIT);
    if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (count <= 0) {
      error = count == 0 ? "connection closed before a reply" : std::string("receive: ") + std::strerror(errno);
      return false;
    }
    input.append(buffer, static_cast<std::size_t>(count));
  }
}

}
}
