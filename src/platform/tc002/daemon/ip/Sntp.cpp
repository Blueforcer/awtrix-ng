#include "platform/tc002/daemon/ip/Sntp.h"

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "platform/tc002/daemon/Process.h"
#include "platform/tc002/daemon/ip/Ipv4.h"

namespace awtrix {
namespace tc002d {
namespace ip {
namespace sntp {
namespace {

constexpr int64_t kNtpToUnixSeconds = INT64_C(2208988800);
constexpr int64_t kNsPerSecond = INT64_C(1000000000);
constexpr int64_t kMaxRoundTripNs = 10 * kNsPerSecond;

uint64_t readTimestamp(const uint8_t* bytes) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) value = (value << 8) | bytes[i];
  return value;
}

void writeTimestamp(uint64_t value, uint8_t* bytes) {
  for (int i = 7; i >= 0; --i) {
    bytes[i] = static_cast<uint8_t>(value & 0xff);
    value >>= 8;
  }
}

}

void encodeRequest(uint64_t cookie, uint8_t (&out)[kPacketSize]) {
  std::memset(out, 0, sizeof(out));
  out[0] = (0 << 6) | (4 << 3) | 3;
  writeTimestamp(cookie, out + 40);
}

bool decodeReply(const uint8_t* data, std::size_t size, uint64_t cookie, Reply& out, const char*& error) {
  out = Reply{};
  if (size < kPacketSize) {
    error = "short reply";
    return false;
  }
  out.leap = data[0] >> 6;
  out.version = (data[0] >> 3) & 7;
  out.mode = data[0] & 7;
  out.stratum = data[1];
  out.originate = readTimestamp(data + 24);
  out.receive = readTimestamp(data + 32);
  out.transmit = readTimestamp(data + 40);
  if (out.mode != 4) error = "not a server reply";
  else if (out.version < 3 || out.version > 4) error = "unsupported version";
  else if (out.originate != cookie) error = "reply to another request";
  else if (out.stratum == 0) error = "kiss-o'-death";
  else if (out.stratum > 15) error = "invalid stratum";
  else if (out.leap == 3) error = "server not synchronized";
  else if (out.transmit == 0) error = "missing transmit time";
  else return true;
  return false;
}

int64_t ntpToUnixNs(uint64_t timestamp) {
  const uint32_t seconds = static_cast<uint32_t>(timestamp >> 32);
  const uint64_t fraction = timestamp & 0xffffffffu;
  int64_t unixSeconds = static_cast<int64_t>(seconds) - kNtpToUnixSeconds;
  if (!(seconds & 0x80000000u)) unixSeconds += INT64_C(1) << 32;
  return unixSeconds * kNsPerSecond + static_cast<int64_t>((fraction * 1000000000u) >> 32);
}

uint64_t unixNsToNtp(int64_t unixNs) {
  int64_t seconds = unixNs / kNsPerSecond;
  int64_t nanos = unixNs % kNsPerSecond;
  if (nanos < 0) {
    nanos += kNsPerSecond;
    --seconds;
  }
  const uint64_t ntpSeconds = static_cast<uint64_t>(seconds + kNtpToUnixSeconds) & 0xffffffffu;
  const uint64_t fraction = (static_cast<uint64_t>(nanos) << 32) / 1000000000u;
  return (ntpSeconds << 32) | fraction;
}

bool sample(const Reply& reply, int64_t sentMonotonicNs, int64_t receivedMonotonicNs,
            int64_t receivedRealtimeNs, Sample& out) {
  const int64_t roundTrip = receivedMonotonicNs - sentMonotonicNs;
  if (roundTrip < 0 || roundTrip > kMaxRoundTripNs) return false;
  const int64_t transmit = ntpToUnixNs(reply.transmit);
  const int64_t receive = reply.receive ? ntpToUnixNs(reply.receive) : transmit;
  int64_t serverTime = transmit - receive;
  if (serverTime < 0 || serverTime > roundTrip) serverTime = 0;
  out.delayNs = roundTrip - serverTime;
  out.offsetNs = transmit + out.delayNs / 2 - receivedRealtimeNs;
  return true;
}

}

namespace {

int64_t clockNs(clockid_t id) {
  timespec now{};
  clock_gettime(id, &now);
  return int64_t(now.tv_sec) * 1000000000 + now.tv_nsec;
}

uint64_t randomCookie(int64_t mix) {
  uint64_t value = 0;
  if (getrandom(&value, sizeof(value), GRND_NONBLOCK) != static_cast<ssize_t>(sizeof(value)))
    value = static_cast<uint64_t>(mix) * UINT64_C(0x9e3779b97f4a7c15) ^ static_cast<uint64_t>(getpid());
  return value ? value : 1;
}

void closeFd(int& fd) {
  if (fd >= 0) ::close(fd);
  fd = -1;
}

int resolveInChild(const std::string& host, int out) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* found = nullptr;
  if (::getaddrinfo(host.c_str(), nullptr, &hints, &found) == 0) {
    unsigned char addresses[16];
    std::size_t size = 0;
    for (addrinfo* entry = found; entry && size < sizeof(addresses); entry = entry->ai_next) {
      if (entry->ai_family != AF_INET) continue;
      std::memcpy(addresses + size, &reinterpret_cast<sockaddr_in*>(entry->ai_addr)->sin_addr, 4);
      size += 4;
    }
    if (size) {
      const ssize_t ignored = ::write(out, addresses, size);
      (void)ignored;
    }
  }
  return 0;
}

}

int64_t RealClock::monotonicNs() { return clockNs(CLOCK_MONOTONIC); }

int64_t RealClock::realtimeNs() { return clockNs(CLOCK_REALTIME); }

bool RealClock::step(int64_t offsetNs) {
  const int64_t target = clockNs(CLOCK_REALTIME) + offsetNs;
  timespec value{};
  value.tv_sec = static_cast<time_t>(target / 1000000000);
  value.tv_nsec = static_cast<long>(target % 1000000000);
  return clock_settime(CLOCK_REALTIME, &value) == 0;
}

bool RealClock::slew(int64_t offsetNs) {
  timeval delta{};
  delta.tv_sec = static_cast<time_t>(offsetNs / 1000000000);
  delta.tv_usec = static_cast<suseconds_t>((offsetNs % 1000000000) / 1000);
  return adjtime(&delta, nullptr) == 0;
}

SntpClient::SntpClient(SystemClock& clock, SntpTiming timing) : clock_(clock), timing_(timing) {}

SntpClient::~SntpClient() { stop(); }

void SntpClient::note(const std::string& message) const {
  if (log) log(message);
}

void SntpClient::setServer(const std::string& server, int64_t nowMs) {
  if (server == server_) return;
  server_ = server;
  cancel();
  retryMs_ = 0;
  failures_ = 0;
  nextAttemptMs_ = online_ ? nowMs : -1;
}

void SntpClient::setFallbackServers(const std::vector<uint32_t>& servers) { fallback_ = servers; }

void SntpClient::setOnline(bool online, int64_t nowMs) {
  if (online == online_) return;
  online_ = online;
  if (!online) {
    cancel();
    nextAttemptMs_ = -1;
    return;
  }
  if (!synchronized_ || nextAttemptMs_ < 0) {
    retryMs_ = 0;
    nextAttemptMs_ = synchronized_ ? nowMs + timing_.pollMs : nowMs;
  }
}

void SntpClient::pollInterest(std::vector<PollInterest>& out) const {
  if (resolverFd_ >= 0) out.push_back({resolverFd_, POLLIN});
  if (socket_ >= 0) out.push_back({socket_, POLLIN});
}

int64_t SntpClient::nextDeadlineMs() const {
  if (phase_ != Phase::Waiting) return deadlineMs_;
  return online_ ? nextAttemptMs_ : -1;
}

void SntpClient::onTime(int64_t nowMs) {
  if (phase_ != Phase::Waiting) {
    if (deadlineMs_ >= 0 && nowMs >= deadlineMs_)
      fail(phase_ == Phase::Resolving ? "name resolution timed out" : "no reply", nowMs);
    return;
  }
  if (online_ && nextAttemptMs_ >= 0 && nowMs >= nextAttemptMs_) attempt(nowMs);
}

void SntpClient::onReady(int fd, short revents, int64_t nowMs) {
  (void)revents;
  if (fd >= 0 && fd == resolverFd_) readResolver(nowMs);
  else if (fd >= 0 && fd == socket_) readReply(nowMs);
}

bool SntpClient::onChildExit(pid_t pid) {
  if (pid <= 0 || pid != resolverPid_) return false;
  resolverPid_ = -1;
  return true;
}

void SntpClient::attempt(int64_t nowMs) {
  const bool useFallback = !fallback_.empty() && (server_.empty() || failures_ % 2 == 1);
  if (useFallback) {
    const uint32_t address = fallback_[(failures_ / 2) % fallback_.size()];
    target_ = formatIpv4(address);
    query(address, nowMs);
    return;
  }
  target_ = server_;
  uint32_t literal = 0;
  if (parseIpv4(server_, literal)) query(literal, nowMs);
  else startResolver(nowMs);
}

void SntpClient::startResolver(int64_t nowMs) {
  if (resolverPid_ > 0) {
    fail("previous resolver still running", nowMs);
    return;
  }
  int ends[2];
  if (::pipe2(ends, O_CLOEXEC | O_NONBLOCK) != 0) {
    fail(std::string("resolver pipe: ") + std::strerror(errno), nowMs);
    return;
  }
  const int out = ends[1];
  const std::string& host = server_;
  const pid_t child = forkTask([&]() { return resolveInChild(host, out); }, SIGKILL, {out});
  ::close(ends[1]);
  if (child < 0) {
    ::close(ends[0]);
    fail(std::string("resolver fork: ") + std::strerror(errno), nowMs);
    return;
  }
  resolverPid_ = child;
  resolverFd_ = ends[0];
  resolvedSize_ = 0;
  phase_ = Phase::Resolving;
  deadlineMs_ = nowMs + timing_.resolveTimeoutMs;
}

void SntpClient::readResolver(int64_t nowMs) {
  for (;;) {
    const ssize_t n = ::read(resolverFd_, resolved_ + resolvedSize_, sizeof(resolved_) - resolvedSize_);
    if (n > 0) {
      resolvedSize_ += static_cast<std::size_t>(n);
      if (resolvedSize_ < sizeof(resolved_)) continue;
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else if (n < 0 && errno == EAGAIN) {
      return;
    }
    break;
  }
  closeFd(resolverFd_);
  if (resolvedSize_ < 4) {
    fail("cannot resolve " + server_, nowMs);
    return;
  }
  uint32_t network = 0;
  std::memcpy(&network, resolved_, 4);
  query(ntohl(network), nowMs);
}

void SntpClient::query(uint32_t address, int64_t nowMs) {
  closeFd(socket_);
  socket_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_port = htons(timing_.port);
  peer.sin_addr.s_addr = htonl(address);
  if (socket_ < 0 || ::connect(socket_, reinterpret_cast<sockaddr*>(&peer), sizeof(peer)) != 0) {
    fail(std::string("socket: ") + std::strerror(errno), nowMs);
    return;
  }
  uint8_t request[sntp::kPacketSize];
  cookie_ = randomCookie(clock_.monotonicNs());
  sntp::encodeRequest(cookie_, request);
  sentMonotonicNs_ = clock_.monotonicNs();
  if (::send(socket_, request, sizeof(request), 0) != static_cast<ssize_t>(sizeof(request))) {
    fail(std::string("send: ") + std::strerror(errno), nowMs);
    return;
  }
  phase_ = Phase::Querying;
  deadlineMs_ = nowMs + timing_.replyTimeoutMs;
}

void SntpClient::readReply(int64_t nowMs) {
  for (int round = 0; round < 4; ++round) {
    uint8_t buffer[128];
    const ssize_t n = ::recv(socket_, buffer, sizeof(buffer), 0);
    const int64_t receivedMonotonic = clock_.monotonicNs();
    const int64_t receivedRealtime = clock_.realtimeNs();
    if (n < 0) {
      if (errno == EINTR) continue;
      if (errno != EAGAIN) fail(std::string("receive: ") + std::strerror(errno), nowMs);
      return;
    }
    sntp::Reply reply;
    sntp::Sample result;
    const char* error = nullptr;
    if (!sntp::decodeReply(buffer, static_cast<std::size_t>(n), cookie_, reply, error)) {
      if (reply.originate == cookie_) {
        fail(std::string("server ") + target_ + ": " + error, nowMs);
        return;
      }
      continue;
    }
    if (!sntp::sample(reply, sentMonotonicNs_, receivedMonotonic, receivedRealtime, result)) {
      fail("implausible round trip", nowMs);
      return;
    }
    succeed(result, nowMs);
    return;
  }
}

void SntpClient::succeed(const sntp::Sample& result, int64_t nowMs) {
  cancel();
  const int64_t magnitude = result.offsetNs < 0 ? -result.offsetNs : result.offsetNs;
  bool adjusted = true;
  if (!synchronized_ || magnitude > timing_.stepThresholdNs) adjusted = clock_.step(result.offsetNs);
  else if (magnitude > timing_.slewThresholdNs) adjusted = clock_.slew(result.offsetNs);
  if (!adjusted) {
    fail(std::string("clock adjustment: ") + std::strerror(errno), nowMs);
    return;
  }
  note("time from " + target_ + ", offset " + std::to_string(result.offsetNs / 1000000) + " ms, delay " +
       std::to_string(result.delayNs / 1000000) + " ms");
  retryMs_ = 0;
  failures_ = 0;
  nextAttemptMs_ = nowMs + timing_.pollMs;
  if (!synchronized_) {
    synchronized_ = true;
    if (synchronizedChanged) synchronizedChanged();
  }
}

void SntpClient::fail(const std::string& reason, int64_t nowMs) {
  cancel();
  ++failures_;
  retryMs_ = retryMs_ ? std::min(retryMs_ * 2, timing_.maxRetryMs) : timing_.firstRetryMs;
  nextAttemptMs_ = nowMs + retryMs_;
  note("sync failed (" + reason + "), retry in " + std::to_string(retryMs_ / 1000) + " s");
}

void SntpClient::cancel() {
  if (resolverPid_ > 0) ::kill(resolverPid_, SIGKILL);
  closeFd(resolverFd_);
  closeFd(socket_);
  phase_ = Phase::Waiting;
  deadlineMs_ = -1;
}

void SntpClient::stop() {
  cancel();
  online_ = false;
  nextAttemptMs_ = -1;
}

}
}
}
