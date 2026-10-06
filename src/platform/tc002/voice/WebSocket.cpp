#include "platform/linux/Auth.h"
#include "platform/posix/Resolver.h"
#include "core/StrCase.h"
#include "core/payload/Base64.h"
#include "platform/tc002/voice/WebSocket.h"

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <deque>
#include <functional>
#include <mutex>
#include <string_view>
#include <thread>

#include "core/render/TextEncoding.h"
#include "platform/linux/tls/TlsTrust.h"

namespace awtrix::tc002::voice {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t kMaxMessage = 65536, kMaxOutgoing = 8192, kMaxEvents = 32;
constexpr std::size_t kMaxEventBytes = 131072, kMaxPackets = 16, kMaxHandshake = 8192;
constexpr auto kOpenTimeout = std::chrono::seconds(5);
constexpr auto kPingAfter = std::chrono::seconds(15), kIdleTimeout = std::chrono::seconds(30);
static_assert(kMaxOutgoing <= 0xFFFF, "outgoing frames use the 16-bit length");

enum class Op : unsigned char { Continuation = 0x0, Text = 0x1, Binary = 0x2, Close = 0x8, Ping = 0x9, Pong = 0xA };

struct Packet {
  std::string bytes;
  bool binary;
  ~Packet() {
    auth::wipe(bytes);
  }
};

std::string_view trimmed(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
  return text;
}

bool hasToken(std::string_view list, std::string_view token) {
  for (;;) {
    const std::size_t comma = list.find(',');
    if (strcase::equalsIgnoreCase(trimmed(list.substr(0, comma)), token)) return true;
    if (comma == std::string_view::npos) return false;
    list.remove_prefix(comma + 1);
  }
}

bool printable(const std::string& text) {
  return std::all_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) > 0x20; });
}

// One masked client frame.
bool encodeFrame(Op op, std::string_view payload, std::string& out) {
  unsigned char mask[4];
  if (RAND_bytes(mask, sizeof mask) != 1) return false;
  out.clear();
  out.reserve(payload.size() + 8);
  out.push_back(static_cast<char>(0x80 | static_cast<unsigned char>(op)));
  if (payload.size() < 126) {
    out.push_back(static_cast<char>(0x80 | payload.size()));
  } else {
    out.push_back(static_cast<char>(0x80 | 126));
    out.push_back(static_cast<char>(payload.size() >> 8));
    out.push_back(static_cast<char>(payload.size() & 0xFF));
  }
  out.append(reinterpret_cast<const char*>(mask), sizeof mask);
  for (std::size_t i = 0; i < payload.size(); ++i) out.push_back(static_cast<char>(payload[i] ^ mask[i & 3]));
  return true;
}

}  // namespace

struct WebSocket::Impl {
  class Connection;
  std::mutex mutex;
  uint64_t generation = 0;
  std::size_t queued = 0, packets = 0, eventBytes = 0;
  bool open = false;
  std::vector<Event> events;
  std::deque<std::function<void()>> tasks;
  const int wake = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  bool quit = false;
  std::unique_ptr<Connection> connection;
  std::thread thread;

  Impl();
  ~Impl();
  void post(std::function<void()> task);
  void run();
  bool current(uint64_t g) {
    std::lock_guard<std::mutex> lock(mutex);
    return g == generation;
  }
  void markOpen(uint64_t g, bool value) {
    std::lock_guard<std::mutex> lock(mutex);
    if (g == generation) open = value;
  }
  void release(uint64_t g, std::size_t size) {
    std::lock_guard<std::mutex> lock(mutex);
    if (g == generation) {
      queued -= size;
      --packets;
    }
  }
  // False when the event overflowed the queue: a Failed event replaced it and the connection must stop.
  bool event(Event e) {
    std::lock_guard<std::mutex> lock(mutex);
    if (e.generation != generation) return true;
    if (events.size() >= kMaxEvents || e.text.size() > kMaxEventBytes - eventBytes) {
      events.clear();
      eventBytes = 0;
      events.push_back({Event::Failed, e.generation, {}});
      open = false;
      return false;
    }
    eventBytes += e.text.size();
    events.push_back(std::move(e));
    return true;
  }
};

// Owned and driven by the worker thread alone.
class WebSocket::Impl::Connection {
 public:
  Connection(Impl& owner, uint64_t generation, Endpoint endpoint)
      : owner_(owner), generation_(generation), endpoint_(std::move(endpoint)) {}
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  ~Connection() {
    stop();
    if (context_) SSL_CTX_free(context_);
  }
  bool stopped() const { return stopped_; }

  void start() {
    deadline_ = Clock::now() + kOpenTimeout;
    if (endpoint_.host.empty() || !printable(endpoint_.host) || endpoint_.port.empty() || !printable(endpoint_.port) ||
        endpoint_.target.empty() || endpoint_.target.front() != '/' || !printable(endpoint_.target))
      return fail();
    if (endpoint_.tls && !(context_ = newTrustedTlsContext())) return fail();
    lookup_ = posix::Resolver::start([host = endpoint_.host, port = endpoint_.port] {
      return posix::Resolver::lookup(host, port);
    });
    if (!lookup_) return fail();
    phase_ = Phase::Resolving;
  }

  void enqueue(std::shared_ptr<Packet> packet) {
    if (stopped_ || phase_ != Phase::Open) return owner_.release(generation_, packet->bytes.size());
    packets_.push_back(std::move(packet));
    flush();
  }

  pollfd watch() const {
    switch (phase_) {
      case Phase::Resolving: return {lookup_->fd(), POLLIN, 0};
      case Phase::Connecting: return {fd_, POLLOUT, 0};
      case Phase::Securing: return {fd_, sslWants_, 0};
      default: return {fd_, static_cast<short>(POLLIN | (writePending() ? POLLOUT : 0)), 0};
    }
  }

  Clock::time_point due() const {
    return phase_ == Phase::Open ? lastRead_ + (pinged_ ? kIdleTimeout : kPingAfter) : deadline_;
  }

  void ready(short revents) {
    switch (phase_) {
      case Phase::Resolving: return resolved();
      case Phase::Connecting: return connected();
      case Phase::Securing: return secure();
      default:
        if (revents & (POLLIN | POLLERR | POLLHUP)) receive();
        if (!stopped_) flush();
    }
  }

  // Past the open or close deadline the connection ends; an open one pings once after 15 s
  // without incoming bytes and gives up after 30 s.
  void tick(Clock::time_point now) {
    if (now < due()) return;
    if (phase_ == Phase::Closing) return finishClose();
    if (phase_ != Phase::Open || pinged_) return fail();
    pinged_ = true;
    queueControl(Op::Ping, {});
    flush();
  }

 private:
  enum class Phase { Idle, Resolving, Connecting, Securing, Upgrading, Open, Closing };

  void resolved() {
    if (!lookup_->take(addresses_)) return;
    lookup_.reset();
    next_ = 0;
    if (addresses_.empty()) return fail();
    tryNext();
  }

  void tryNext() {
    closeSocket();
    auto result = posix::connectNext(addresses_, next_);
    fd_ = result.fd.release();
    if (result.state == posix::ConnectState::Connected) return established();
    if (result.state == posix::ConnectState::Pending) {
      phase_ = Phase::Connecting;
      return;
    }
    fail();
  }

  void connected() {
    int error = 0;
    socklen_t size = sizeof error;
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error != 0) return tryNext();
    established();
  }

  void established() {
    addresses_.clear();
    next_ = 0;
    lookup_.reset();
    const int on = 1;
    if (::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on) != 0) return fail();
    if (!endpoint_.tls) return upgrade();
    ssl_ = SSL_new(context_);
    if (!ssl_ || SSL_set_fd(ssl_, fd_) != 1 || !expectTlsPeer(ssl_, endpoint_.host)) return fail();
    SSL_set_mode(ssl_, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    phase_ = Phase::Securing;
    secure();
  }

  void secure() {
    const int result = SSL_connect(ssl_);
    if (result == 1) return upgrade();
    const int error = SSL_get_error(ssl_, result);
    if (error == SSL_ERROR_WANT_READ)
      sslWants_ = POLLIN;
    else if (error == SSL_ERROR_WANT_WRITE)
      sslWants_ = POLLOUT;
    else
      fail();
  }

  void upgrade() {
    unsigned char nonce[16];
    if (RAND_bytes(nonce, sizeof nonce) != 1) return fail();
    const std::string key = base64::encode(nonce, sizeof nonce);
    const std::string proof = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(proof.data()), proof.size(), digest);
    accept_ = base64::encode(digest, sizeof digest);
    const bool literal6 = endpoint_.host.find(':') != std::string::npos;
    out_ = "GET " + endpoint_.target + " HTTP/1.1\r\nHost: " +
           (literal6 ? "[" + endpoint_.host + "]" : endpoint_.host) + ":" + endpoint_.port +
           "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
           "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    offset_ = 0;
    phase_ = Phase::Upgrading;
    flush();
  }

  // Bytes moved, 0 while the socket would block, -1 on an error or the peer's end of stream.
  long read(char* buffer, std::size_t size) {
    readWantsWrite_ = false;
    if (!ssl_) {
      const ssize_t n = ::recv(fd_, buffer, size, MSG_DONTWAIT);
      if (n > 0) return n;
      return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
    }
    const int n = SSL_read(ssl_, buffer, static_cast<int>(size));
    if (n > 0) return n;
    const int error = SSL_get_error(ssl_, n);
    if (error == SSL_ERROR_WANT_READ) return 0;
    if (error != SSL_ERROR_WANT_WRITE) return -1;
    readWantsWrite_ = true;
    return 0;
  }

  long write(const char* data, std::size_t size) {
    if (!ssl_) {
      const ssize_t n = ::send(fd_, data, size, MSG_DONTWAIT | MSG_NOSIGNAL);
      if (n >= 0) return n;
      return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    }
    const int n = SSL_write(ssl_, data, static_cast<int>(size));
    if (n > 0) return n;
    const int error = SSL_get_error(ssl_, n);
    return error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE ? 0 : -1;
  }

  void receive() {
    char buffer[4096];
    while (!stopped_) {
      const long n = read(buffer, sizeof buffer);
      if (n < 0) return phase_ == Phase::Closing ? finishClose() : fail();
      if (n == 0 || phase_ == Phase::Closing) return;
      in_.append(buffer, static_cast<std::size_t>(n));
      lastRead_ = Clock::now();
      pinged_ = false;
      if (!(phase_ == Phase::Upgrading ? handshake() : frames())) return fail();
    }
  }

  bool handshake() {
    const std::size_t end = in_.find("\r\n\r\n");
    if (end == std::string::npos) return in_.size() <= kMaxHandshake;
    if (end > kMaxHandshake || !acceptable(std::string_view(in_).substr(0, end))) return false;
    in_.erase(0, end + 4);
    phase_ = Phase::Open;
    owner_.markOpen(generation_, true);
    if (!owner_.event({Event::Open, generation_, {}})) {
      stop();
      return true;
    }
    return frames();
  }

  bool acceptable(std::string_view head) const {
    const auto line = [&head] {
      const std::size_t end = head.find("\r\n");
      const std::string_view text = head.substr(0, end);
      head.remove_prefix(end == std::string_view::npos ? head.size() : end + 2);
      return text;
    };
    const std::string_view status = line();
    if (status.substr(0, 12) != "HTTP/1.1 101" || (status.size() > 12 && status[12] != ' ')) return false;
    bool upgrade = false, connection = false, accepted = false;
    while (!head.empty()) {
      const std::string_view field = line();
      const std::size_t colon = field.find(':');
      if (colon == std::string_view::npos) return false;
      const std::string_view name = field.substr(0, colon), value = trimmed(field.substr(colon + 1));
      if (strcase::equalsIgnoreCase(name, "Upgrade"))
        upgrade = strcase::equalsIgnoreCase(value, "websocket");
      else if (strcase::equalsIgnoreCase(name, "Connection"))
        connection = hasToken(value, "upgrade");
      else if (strcase::equalsIgnoreCase(name, "Sec-WebSocket-Accept"))
        accepted = value == accept_;
      else if (strcase::equalsIgnoreCase(name, "Sec-WebSocket-Extensions") || strcase::equalsIgnoreCase(name, "Sec-WebSocket-Protocol"))
        return false;
    }
    return upgrade && connection && accepted;
  }

  // Only text messages reach the session; binary data, reserved bits and opcodes, masked server
  // frames and messages above 64 KiB end the connection.
  bool frames() {
    while (phase_ == Phase::Open && in_.size() >= 2) {
      const auto* head = reinterpret_cast<const unsigned char*>(in_.data());
      const bool final = head[0] & 0x80;
      const auto op = static_cast<Op>(head[0] & 0x0F);
      if ((head[0] & 0x70) || (head[1] & 0x80)) return false;
      std::size_t header = 2;
      uint64_t length = head[1] & 0x7F;
      if (length == 126) {
        if (in_.size() < 4) return true;
        length = (uint64_t{head[2]} << 8) | head[3];
        header = 4;
      } else if (length == 127) {
        if (in_.size() < 10) return true;
        length = 0;
        for (std::size_t i = 2; i < 10; ++i) length = (length << 8) | head[i];
        header = 10;
      }
      switch (op) {
        case Op::Text:
          if (assembling_ || length > kMaxMessage) return false;
          break;
        case Op::Continuation:
          if (!assembling_ || length > kMaxMessage - message_.size()) return false;
          break;
        case Op::Close:
        case Op::Ping:
        case Op::Pong:
          if (!final || length > 125 || (op == Op::Close && length == 1)) return false;
          break;
        default:
          return false;
      }
      if (in_.size() - header < length) return true;
      const std::string_view payload(in_.data() + header, static_cast<std::size_t>(length));
      if (op == Op::Text || op == Op::Continuation) {
        message_.append(payload);
        assembling_ = !final;
        if (final && !deliver()) return false;
      } else if (op == Op::Ping) {
        queueControl(Op::Pong, payload);
      } else if (op == Op::Close) {
        queueControl(Op::Close, payload.substr(0, 2));
        phase_ = Phase::Closing;
        deadline_ = Clock::now() + kOpenTimeout;
      }
      if (stopped_) return true;
      in_.erase(0, header + static_cast<std::size_t>(length));
    }
    return true;
  }

  bool deliver() {
    if (!awtrix::text::isValidUtf8(message_)) return false;
    std::string text;
    text.swap(message_);
    if (!owner_.event({Event::Text, generation_, std::move(text)})) stop();
    return true;
  }

  void queueControl(Op op, std::string_view payload) {
    std::string frame;
    if (!encodeFrame(op, payload, frame)) return fail();
    control_.push_back(std::move(frame));
  }

  bool writePending() const {
    return offset_ < out_.size() || !control_.empty() || (phase_ == Phase::Open && !packets_.empty()) ||
           readWantsWrite_;
  }

  void flush() {
    while (!stopped_) {
      if (offset_ == out_.size()) {
        finishWrite();
        if (!nextWrite()) break;
      }
      const long n = write(out_.data() + offset_, out_.size() - offset_);
      if (n < 0) return phase_ == Phase::Closing ? finishClose() : fail();
      if (n == 0) return;
      offset_ += static_cast<std::size_t>(n);
    }
    if (!stopped_ && phase_ == Phase::Closing && !writePending()) finishClose();
  }

  void finishWrite() {
    auth::wipe(out_);
    offset_ = 0;
    if (writing_) owner_.release(generation_, writing_);
    writing_ = 0;
  }

  bool nextWrite() {
    if (!control_.empty()) {
      out_ = std::move(control_.front());
      control_.pop_front();
      return true;
    }
    if (phase_ != Phase::Open || packets_.empty()) return false;
    const std::shared_ptr<Packet> packet = std::move(packets_.front());
    packets_.pop_front();
    writing_ = packet->bytes.size();
    if (encodeFrame(packet->binary ? Op::Binary : Op::Text, packet->bytes, out_)) return true;
    fail();
    return false;
  }

  void finishClose() {
    if (stopped_) return;
    owner_.markOpen(generation_, false);
    stop();
    owner_.event({Event::Closed, generation_, {}});
  }

  void fail() {
    if (stopped_) return;
    ERR_clear_error();
    owner_.markOpen(generation_, false);
    stop();
    owner_.event({Event::Failed, generation_, {}});
  }

  void stop() {
    if (stopped_) return;
    stopped_ = true;
    phase_ = Phase::Idle;
    addresses_.clear();
    next_ = 0;
    lookup_.reset();
    closeSocket();
    if (writing_) owner_.release(generation_, writing_);
    writing_ = 0;
    for (const auto& packet : packets_) owner_.release(generation_, packet->bytes.size());
    packets_.clear();
    auth::wipe(out_);
    offset_ = 0;
    for (auto& frame : control_) auth::wipe(frame);
    control_.clear();
    auth::wipe(in_);
    auth::wipe(message_);
  }

  void closeSocket() {
    if (ssl_) SSL_free(ssl_);
    ssl_ = nullptr;
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }

  Impl& owner_;
  const uint64_t generation_;
  const Endpoint endpoint_;
  Phase phase_ = Phase::Idle;
  bool stopped_ = false, pinged_ = false, assembling_ = false, readWantsWrite_ = false;
  short sslWants_ = POLLIN;
  Clock::time_point deadline_, lastRead_ = Clock::now();
  std::shared_ptr<posix::Resolver> lookup_;
  posix::Resolver::Addresses addresses_;
  std::size_t next_ = 0;
  int fd_ = -1;
  SSL_CTX* context_ = nullptr;
  SSL* ssl_ = nullptr;
  std::string accept_, out_, in_, message_;
  std::size_t offset_ = 0, writing_ = 0;
  std::deque<std::string> control_;
  std::deque<std::shared_ptr<Packet>> packets_;
};

WebSocket::Impl::Impl() : thread([this] { run(); }) {}

WebSocket::Impl::~Impl() {
  post([this] { quit = true; });
  thread.join();
  if (wake >= 0) ::close(wake);
}

void WebSocket::Impl::post(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(mutex);
    tasks.push_back(std::move(task));
  }
  const uint64_t one = 1;
  if (wake >= 0 && ::write(wake, &one, sizeof one) < 0) return;
}

void WebSocket::Impl::run() {
  while (!quit) {
    std::deque<std::function<void()>> batch;
    {
      std::lock_guard<std::mutex> lock(mutex);
      batch.swap(tasks);
    }
    for (auto& task : batch) task();
    if (quit) break;
    if (connection && connection->stopped()) connection.reset();
    pollfd fds[2] = {{wake, POLLIN, 0}, {-1, 0, 0}};
    long long timeout = wake < 0 ? 20 : -1;
    if (connection) {
      fds[1] = connection->watch();
      const long long due =
          std::chrono::ceil<std::chrono::milliseconds>(connection->due() - Clock::now()).count();
      timeout = std::clamp<long long>(due, 0, timeout < 0 ? INT_MAX : timeout);
    }
    if (::poll(fds, 2, static_cast<int>(timeout)) < 0) continue;
    uint64_t count = 0;
    if ((fds[0].revents & POLLIN) && ::read(wake, &count, sizeof count) < 0) count = 0;
    if (!connection || connection->stopped()) continue;
    if (fds[1].revents) connection->ready(fds[1].revents);
    if (!connection->stopped()) connection->tick(Clock::now());
  }
  connection.reset();
}

WebSocket::WebSocket() : impl_(new Impl) {}
WebSocket::~WebSocket() = default;

uint64_t WebSocket::connect(const Endpoint& endpoint) {
  cancel();
  uint64_t generation;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    generation = impl_->generation;
  }
  impl_->post([p = impl_.get(), generation, endpoint] {
    if (!p->current(generation)) return;
    p->connection = std::make_unique<Impl::Connection>(*p, generation, endpoint);
    p->connection->start();
  });
  return generation;
}

void WebSocket::cancel() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ++impl_->generation;
    impl_->open = false;
    impl_->queued = 0;
    impl_->packets = 0;
    impl_->events.clear();
    impl_->eventBytes = 0;
  }
  impl_->post([p = impl_.get()] { p->connection.reset(); });
}

bool WebSocket::send(std::string bytes, bool binary) {
  uint64_t generation;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->open || bytes.empty() || bytes.size() > kMaxOutgoing - impl_->queued ||
        impl_->packets >= kMaxPackets) {
      auth::wipe(bytes);
      return false;
    }
    impl_->queued += bytes.size();
    ++impl_->packets;
    generation = impl_->generation;
  }
  auto packet = std::make_shared<Packet>();
  packet->bytes = std::move(bytes);
  packet->binary = binary;
  impl_->post([p = impl_.get(), generation, packet] {
    if (!p->current(generation)) return;
    if (!p->connection) return p->release(generation, packet->bytes.size());
    p->connection->enqueue(packet);
  });
  return true;
}

std::vector<WebSocket::Event> WebSocket::poll() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<Event> out;
  out.swap(impl_->events);
  impl_->eventBytes = 0;
  return out;
}
}  // namespace awtrix::tc002::voice
