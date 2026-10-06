#include "platform/linux/LinuxMqttTls.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "platform/posix/Resolver.h"
#include "platform/linux/tls/TlsPolicy.h"
#include "platform/linux/tls/TlsTrust.h"
#include "system/Log.h"

namespace awtrix {
namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t kMaxPacket = 8192;
constexpr size_t kMaxQueuedBytes = 256 * 1024;
constexpr auto kIoDeadline = std::chrono::seconds(2);
}

struct LinuxMqttTls::Impl {
  struct Write { std::vector<uint8_t> data; size_t offset = 0; Clock::time_point queued = Clock::now(); };
  tls::ContextPtr context{nullptr, SSL_CTX_free};
  std::shared_ptr<tls::PeerTrust> trust;
  SSL* ssl = nullptr;
  int fd = -1;
  std::string peerName;
  std::deque<Write> output;
  size_t outputBytes = 0;
  std::vector<uint8_t> input;
  size_t offset = 0, readyUntil = 0;
  Clock::time_point inputStarted{};
  std::thread handshake;
  std::atomic<int> handshakeResult{-1};
  std::atomic<bool> cancelled{false};
  bool handshaking = false; // access only by the thread currently owning the client
  // The fatal alert the broker sent before the last close, or -1; set by the thread owning the
  // client, logged by the loop thread. reported and repeats throttle a reason that keeps coming.
  int alert = -1, reported = -1;
  unsigned repeats = 0;

  void close() {
    if (ssl) { SSL_free(ssl); ssl = nullptr; }
    if (fd >= 0) { ::close(fd); fd = -1; }
    output.clear(); outputBytes = 0; input.clear(); offset = readyUntil = 0;
  }

  // Closes after a TLS failure and keeps the alert the broker sent, if any, from this thread's
  // error queue, which it empties.
  void fail() {
    alert = -1;
    while (const unsigned long code = ERR_get_error()) {
      const int reason = ERR_GET_REASON(code);
      if (ERR_GET_LIB(code) == ERR_LIB_SSL && reason > SSL_AD_REASON_OFFSET && reason <= SSL_AD_REASON_OFFSET + 255)
        alert = reason - SSL_AD_REASON_OFFSET;
    }
    close();
  }

  // A broker that refuses the client after the handshake sends its alert and resets the
  // connection, so a write can run into the reset while the alert still waits to be read.
  void failWrite() {
    ERR_clear_error();
    uint8_t unused;
    SSL_read(ssl, &unused, 1);
    fail();
  }

  // Loop thread only. A reason that repeats is logged again every tenth time, as MqttLink logs
  // its own failures.
  void report() {
    if (alert < 0) return;
    repeats = alert == reported ? repeats + 1 : 0;
    reported = alert;
    alert = -1;
    if (repeats % 10) return;
    if (reported == SSL_AD_CERTIFICATE_REQUIRED) logf("mqtt: broker requires a client certificate");
    else logf("mqtt: broker closed TLS: %s", SSL_alert_desc_string_long(reported));
  }

  bool wait(short events, Clock::time_point deadline) {
    while (!cancelled.load() && Clock::now() < deadline) {
      pollfd descriptor{fd, events, 0};
      const int result = ::poll(&descriptor, 1, 20);
      if (result > 0) return (descriptor.revents & events) != 0;
      if (result < 0 && errno != EINTR) return false;
    }
    return false;
  }

  void frame() {
    if (readyUntil > offset || input.empty()) return;
    size_t length = 0, multiplier = 1, position = 1;
    for (; position < input.size() && position <= 4; ++position) {
      const uint8_t digit = input[position];
      length += (digit & 127) * multiplier;
      if (length + position + 1 > kMaxPacket) { close(); return; }
      if (!(digit & 128)) {
        if (input.size() >= length + position + 1) readyUntil = length + position + 1;
        return;
      }
      multiplier *= 128;
    }
    if (position > 4) close();
  }

  // No waits or DNS on the render thread. TLS retries keep the same front-buffer
  // arguments, and a peer cannot retain a partial packet/write indefinitely.
  void pump() {
    if (!ssl || cancelled.load()) return;
    const auto now = Clock::now();
    if ((!output.empty() && now - output.front().queued >= kIoDeadline) ||
        (!input.empty() && readyUntil == 0 && now - inputStarted >= kIoDeadline)) { close(); return; }
    size_t operations = 0;
    while (!output.empty() && operations++ < 8) {
      auto& write = output.front();
      const int n = SSL_write(ssl, write.data.data() + write.offset, static_cast<int>(write.data.size() - write.offset));
      if (n <= 0) {
        const int error = SSL_get_error(ssl, n);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) failWrite();
        return;
      }
      write.offset += static_cast<size_t>(n);
      outputBytes -= static_cast<size_t>(n);
      if (write.offset == write.data.size()) output.pop_front();
    }
    if (readyUntil > offset) return;
    std::array<uint8_t, 16384> bytes{};
    for (int i = 0; i < 4 && ssl; ++i) {
      const int n = SSL_read(ssl, bytes.data(), static_cast<int>(bytes.size()));
      if (n <= 0) {
        const int error = SSL_get_error(ssl, n);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) fail();
        return;
      }
      if (input.empty()) inputStarted = Clock::now();
      input.insert(input.end(), bytes.begin(), bytes.begin() + n);
      frame();
      if (readyUntil > offset) return;
    }
  }
};

LinuxMqttTls::LinuxMqttTls(std::shared_ptr<tls::PeerTrust> trust) : impl_(std::make_unique<Impl>()) {
  impl_->trust = std::move(trust);
  if (impl_->trust) impl_->context = tls::newClientContext();
}

LinuxMqttTls::~LinuxMqttTls() { shutdown(); }

bool LinuxMqttTls::valid() const { return impl_->context != nullptr; }
void LinuxMqttTls::setPeerName(const std::string& host) { impl_->peerName = host; }

net::ResolveState LinuxMqttTls::connectStep(std::function<bool()> handshake) {
  auto& state = *impl_;
  if (!state.handshake.joinable()) {
    state.cancelled.store(false);
    state.handshakeResult.store(-1);
    state.handshake = std::thread([this, handshake = std::move(handshake)] {
      impl_->handshaking = true;
      bool result = false;
      try { result = handshake(); } catch (...) { stop(); }
      impl_->handshaking = false;
      impl_->handshakeResult.store(result ? 1 : 0, std::memory_order_release);
    });
    return net::ResolveState::Pending;
  }
  const int result = state.handshakeResult.load(std::memory_order_acquire);
  if (result < 0) return net::ResolveState::Pending;
  state.handshake.join();
  if (result == 1) {
    state.reported = -1;
    return net::ResolveState::Ready;
  }
  state.report();
  return net::ResolveState::Failed;
}

void LinuxMqttTls::shutdown() {
  impl_->cancelled.store(true);
  if (impl_->handshake.joinable()) impl_->handshake.join();
  impl_->close();
}

int LinuxMqttTls::connect(IPAddress ip, uint16_t port) { return connect(ip.toString().c_str(), port); }

int LinuxMqttTls::connect(const char* address, uint16_t port) {
  auto& state = *impl_;
  state.close();
  if (!valid() || !address || state.peerName.empty() || state.cancelled.load()) return 0;
  sockaddr_in remote{};
  remote.sin_family = AF_INET;
  remote.sin_port = htons(port);
  if (::inet_pton(AF_INET, address, &remote.sin_addr) != 1) return 0;
  posix::SocketAddress target;
  target.family = AF_INET;
  target.size = sizeof remote;
  std::memcpy(&target.addr, &remote, sizeof remote);
  std::size_t next = 0;
  auto connection = posix::connectNext(&target, 1, next);
  state.fd = connection.fd.release();
  if (state.fd < 0) return 0;
  const auto deadline = Clock::now() + std::chrono::seconds(3);
  if (connection.state != posix::ConnectState::Connected &&
      (!state.wait(POLLOUT, deadline) || posix::connectionError(state.fd) != 0)) {
    state.close();
    return 0;
  }
  const int one = 1;
  ::setsockopt(state.fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  state.ssl = SSL_new(state.context.get());
  if (!state.ssl || SSL_set_fd(state.ssl, state.fd) != 1 || !tls::expectPeer(state.ssl, state.peerName)) {
    state.close();
    return 0;
  }
  while (!state.cancelled.load() && Clock::now() < deadline) {
    const int result = SSL_connect(state.ssl);
    if (result == 1) {
      if (state.trust->check(state.ssl, state.peerName) == tls::PeerTrust::Verdict::Trusted) return 1;
      break;
    }
    const int error = SSL_get_error(state.ssl, result);
    if ((error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) ||
        !state.wait(error == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, deadline)) break;
  }
  state.fail();
  return 0;
}

size_t LinuxMqttTls::write(uint8_t value) { return write(&value, 1); }
size_t LinuxMqttTls::write(const uint8_t* buffer, size_t size) {
  auto& state = *impl_;
  if (!state.ssl || state.cancelled.load() || !buffer || size > kMaxQueuedBytes - state.outputBytes) {
    state.close(); return 0;
  }
  if (size) state.output.push_back({std::vector<uint8_t>(buffer, buffer + size)});
  state.outputBytes += size;
  state.pump();
  return state.ssl ? size : 0;
}

int LinuxMqttTls::available() {
  auto& state = *impl_;
  if (state.readyUntil > state.offset) return static_cast<int>(state.readyUntil - state.offset);
  state.pump();
  const int ready = static_cast<int>(state.readyUntil - state.offset);
  if (!ready && state.handshaking) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return ready;
}

int LinuxMqttTls::read() { uint8_t value; return read(&value, 1) == 1 ? value : -1; }
int LinuxMqttTls::read(uint8_t* buffer, size_t size) {
  auto& state = *impl_;
  const int ready = available();
  if (!buffer || ready <= 0) return 0;
  const size_t count = std::min(size, static_cast<size_t>(ready));
  std::memcpy(buffer, state.input.data() + state.offset, count);
  state.offset += count;
  if (state.offset == state.readyUntil) {
    state.input.erase(state.input.begin(), state.input.begin() + static_cast<std::ptrdiff_t>(state.offset));
    state.offset = state.readyUntil = 0;
    state.inputStarted = Clock::now();
    state.frame();
  }
  return static_cast<int>(count);
}
int LinuxMqttTls::peek() { return available() > 0 ? impl_->input[impl_->offset] : -1; }
void LinuxMqttTls::flush() { impl_->pump(); }
void LinuxMqttTls::stop() { impl_->close(); }
uint8_t LinuxMqttTls::connected() {
  impl_->pump();
  if (!impl_->handshaking) impl_->report();
  return impl_->ssl && !impl_->cancelled.load() ? 1 : 0;
}
}
