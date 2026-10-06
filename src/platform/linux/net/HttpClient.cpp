#include "platform/linux/net/HttpClient.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/tls/TlsTrust.h"
#include "core/net/Url.h"

namespace awtrix::net {

bool splitUrl(const std::string& url, std::string& origin, std::string& target) {
  const auto parsed = parseUrl(url);
  if (!parsed || parsed->userinfo) return false;
  origin = parsed->origin();
  target = parsed->requestTarget();
  return true;
}

void configure(httplib::Client& client, const ClientLimits& limits) {
  client.set_connection_timeout(limits.connectTimeoutSec, 0);
  client.set_read_timeout(limits.ioTimeoutSec, 0);
  client.set_write_timeout(limits.ioTimeoutSec, 0);
  client.set_max_timeout(std::chrono::milliseconds(limits.timeoutMs));
  client.set_follow_location(limits.followRedirects);
  client.enable_server_certificate_verification(true);
  useTlsTrust(client);
}

void startClock(httplib::Request& request) { request.start_time_ = std::chrono::steady_clock::now(); }

httplib::Result send(httplib::Client& client, const std::string& method,
                     const std::string& path, const std::string& body,
                     const RequestHeaders& headers, const BodyReceiver& receive) {
  httplib::Request request;
  request.method = method;
  request.path = path;
  request.body = body;
  startClock(request);
  for (const auto& [name, value] : headers) request.headers.emplace(name, value);
  std::size_t received = 0;
  request.content_receiver = [&](const char* bytes, std::size_t count, uint64_t, uint64_t) {
    if (count > 1024 * 1024 - received) return false;
    received += count;
    return receive(bytes, count);
  };
  return client.send(request);
}

httplib::Result receive200(httplib::Client& client, const std::string& path,
                           const BodyPolicy& policy, ReceivedBody& state,
                           const BodyReceiver& receive) {
  state = {};
  const auto active = [&] { return !policy.active || policy.active(); };
  const auto fits = [&](uint64_t have, uint64_t more) {
    if (more > policy.maxBytes - have) {
      state.tooLarge = true;
      return false;
    }
    return !policy.room || policy.room(static_cast<std::size_t>(more));
  };
  return client.Get(path, {}, [&](const httplib::Response& response) {
    state.status = response.status;
    if (response.status != 200 || !active()) return false;
    return !response.has_header("Content-Length") ||
        fits(0, std::strtoull(response.get_header_value("Content-Length").c_str(), nullptr, 10));
  }, [&](const char* bytes, std::size_t count) {
    if (!active() || !fits(state.size, count)) return false;
    state.size += count;
    return receive(bytes, count);
  });
}

SocketInterrupt::~SocketInterrupt() { release(); }

void SocketInterrupt::watch(httplib::Client& client) {
  client.set_socket_options([this](int socket) { adopt(socket); });
}

void SocketInterrupt::adopt(int socket) {
  const int copy = ::fcntl(socket, F_DUPFD_CLOEXEC, 0);
  std::lock_guard<std::mutex> lock(mutex_);
  if (interrupted_) {
    ::shutdown(socket, SHUT_RDWR);
    if (copy >= 0) ::close(copy);
    return;
  }
  closeCopy();
  socket_ = copy;
}

void SocketInterrupt::interrupt() {
  std::lock_guard<std::mutex> lock(mutex_);
  interrupted_ = true;
  if (socket_ >= 0) ::shutdown(socket_, SHUT_RDWR);
}

void SocketInterrupt::rearm() {
  std::lock_guard<std::mutex> lock(mutex_);
  interrupted_ = false;
  closeCopy();
}

void SocketInterrupt::release() {
  std::lock_guard<std::mutex> lock(mutex_);
  closeCopy();
}

void SocketInterrupt::closeCopy() {
  if (socket_ < 0) return;
  ::close(socket_);
  socket_ = -1;
}

}
