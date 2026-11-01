#include "platform/linux/net/HttpClient.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string_view>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/tls/TlsTrust.h"
#include "core/net/Url.h"
#include "core/script/HttpBodyFilter.h"
#include "platform/posix/Files.h"

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
  client.set_default_headers({{"User-Agent", "AWTRIX-NG"}});
  client.enable_server_certificate_verification(true);
  useTlsTrust(client);
}

namespace {

void startClock(httplib::Request& request) { request.start_time_ = std::chrono::steady_clock::now(); }

void attach(httplib::Request& request, RequestBody& body) {
  if (!body.file) {
    request.body = std::move(body.head);
    request.body += body.tail;
    return;
  }
  const std::array<std::string_view, 3> parts{body.head, body.file->bytes(), body.tail};
  request.content_length_ = parts[0].size() + parts[1].size() + parts[2].size();
  request.content_provider_ = [parts](std::size_t offset, std::size_t, httplib::DataSink& sink) {
    for (const std::string_view part : parts) {
      if (offset < part.size()) return sink.write(part.data() + offset, part.size() - offset);
      offset -= part.size();
    }
    return false;
  };
}

}

httplib::Result exchange(httplib::Client& client, const std::string& method, const std::string& path,
                         RequestBody body, const RequestHeaders& headers, const BodyPolicy& policy,
                         ResponseState& state, const BodyReceiver& receive) {
  state = {};
  httplib::Request request;
  request.method = method;
  request.path = path;
  attach(request, body);
  startClock(request);
  for (const auto& [name, value] : headers) request.headers.emplace(name, value);
  const auto fits = [&](uint64_t more) {
    if (more > policy.maxBytes - state.receivedBytes) {
      state.tooLarge = true;
      return false;
    }
    return !policy.room || policy.room(static_cast<std::size_t>(more));
  };
  request.response_handler = [&](const httplib::Response& response) {
    state.status = response.status;
    state.contentType = response.get_header_value("Content-Type");
    if (policy.accept && !policy.accept(state.status, state.contentType)) return false;
    if (!response.has_header("Content-Length")) return true;
    const uint64_t announced = std::strtoull(response.get_header_value("Content-Length").c_str(), nullptr, 10);
    if (!fits(announced)) return false;
    state.announcedBytes = static_cast<std::size_t>(announced);
    return true;
  };
  request.content_receiver = [&](const char* bytes, std::size_t count, uint64_t, uint64_t) {
    if (!fits(count)) return false;
    state.receivedBytes += count;
    return receive(bytes, count);
  };
  auto response = std::make_unique<httplib::Response>();
  httplib::Error error = httplib::Error::Success;
  if (!client.send(request, *response, error)) response.reset();
  if (response && state.status == 0) {
    state.status = response->status;
    state.contentType = response->get_header_value("Content-Type");
  }
  return httplib::Result(std::move(response), error, std::move(request.headers));
}

httplib::Result send(httplib::Client& client, const std::string& method, const std::string& path,
                     RequestBody body, const RequestHeaders& headers, const BodyReceiver& receive) {
  ResponseState state;
  return exchange(client, method, path, std::move(body), headers, {kMaxAnswerBytes, {}, {}}, state, receive);
}

FilteredAnswer sendFiltered(httplib::Client& client, const std::string& method, const std::string& path,
                            RequestBody body, const RequestHeaders& headers, const AnswerFilter& filter) {
  script::HttpBodyFilter kept;
  kept.begin(filter.find, filter.keep, script::bodyCapWithinHeapGrowth(filter.cap));
  const auto response = send(client, method, path, std::move(body), headers,
                             [&](const char* bytes, std::size_t count) {
    kept.feed(bytes, count);
    return !kept.outOfRoom();
  });
  FilteredAnswer answer;
  if (!response) return answer;
  answer.status = response->status;
  answer.ok = kept.matched() && !kept.outOfRoom();
  if (answer.ok) answer.body = std::move(kept.body());
  return answer;
}

httplib::Result receive200(httplib::Client& client, const std::string& path,
                           const BodyPolicy& policy, ResponseState& state,
                           const BodyReceiver& receive) {
  const BodyPolicy only200{policy.maxBytes, [&](int status, const std::string& contentType) {
    return status == httplib::StatusCode::OK_200 && (!policy.accept || policy.accept(status, contentType));
  }, policy.room};
  return exchange(client, "GET", path, {}, {}, only200, state, receive);
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
